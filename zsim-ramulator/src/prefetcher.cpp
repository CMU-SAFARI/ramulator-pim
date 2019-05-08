/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "prefetcher.h"
#include "bithacks.h"

//#define DBG(args...) info(args)
#define DBG(args...)

#define LQ_THRESHOLD 32


void StreamPrefetcher::setParents(uint32_t _childId, const g_vector<MemObject*>& _parents, Network* network) {
    childId = _childId;
    printf("----[StreamPrefetcher] Parents: %d\n",_parents.size());
    //if (parents.size() != 1) panic("Must have one parent");
    if (network) panic("Network not handled");
    //parent = parents[0];
    numparents = _parents.size();
    for(int i = 0; i < numparents; i++){
        parents.push_back(_parents[i]);
    }
}

void StreamPrefetcher::setChildren(const g_vector<BaseCache*>& _children, Network* network) {
    printf("---- Chidren: %d\n", _children.size());
    if (_children.size() != 1) panic("Must have one children");
    if (network) panic("Network not handled");
    numchildren = _children.size();
    for(int i = 0; i < numchildren; i++){
        children.push_back(_children[i]);
    }
    //child = _children[0];
}

void StreamPrefetcher::initStats(AggregateStat* parentStat) {
    AggregateStat* s = new AggregateStat();
    s->init(name.c_str(), "Prefetcher stats");
    profAccesses.init("acc", "Accesses"); s->append(&profAccesses);
    profPrefetches.init("pf", "Issued prefetches"); s->append(&profPrefetches);
    profDoublePrefetches.init("dpf", "Issued double prefetches"); s->append(&profDoublePrefetches);
    profPageHits.init("pghit", "Page/entry hit"); s->append(&profPageHits);
    profHits.init("hit", "Prefetch buffer hits, short and full"); s->append(&profHits);
    profShortHits.init("shortHit", "Prefetch buffer short hits"); s->append(&profShortHits);
    profStrideSwitches.init("strideSwitches", "Predicted stride switches"); s->append(&profStrideSwitches);
    profLowConfAccs.init("lcAccs", "Low-confidence accesses with no prefetches"); s->append(&profLowConfAccs);
    parentStat->append(s);
}

uint64_t StreamPrefetcher::access(MemReq& req) {
    uint32_t origChildId = req.childId;
    req.childId = childId;

    uint32_t parentId = getParentId(req.lineAddr);
    if (req.type != GETS) return parents[parentId]->access(req); //other reqs ignored, including stores

    profAccesses.inc();

    uint64_t reqCycle = req.cycle;
    uint64_t respCycle = parents[parentId]->access(req);

    Address pageAddr = req.lineAddr >> 6;
    uint32_t pos = req.lineAddr & (64-1);
    uint32_t idx = 16;
    // This loop gets unrolled and there are no control dependences. Way faster than a break (but should watch for the avoidable loop-carried dep)
    for (uint32_t i = 0; i < 16; i++) {
        bool match = (pageAddr == tag[i]);
        idx = match?  i : idx;  // ccmov, no branch
    }

    DBG("%s: 0x%lx page %lx pos %d", name.c_str(), req.lineAddr, pageAddr, pos);

    if (idx == 16) {  // entry miss
        uint32_t cand = 16;
        uint64_t candScore = -1;
        //uint64_t candScore = 0;
        for (uint32_t i = 0; i < 16; i++) {
            if (array[i].lastCycle > reqCycle + 500) continue;  // warm prefetches, not even a candidate
            /*uint64_t score = (reqCycle - array[i].lastCycle)*(3 - array[i].conf.counter());
            if (score > candScore) {
                cand = i;
                candScore = score;
            }*/
            if (array[i].ts < candScore) {  // just LRU
                cand = i;
                candScore = array[i].ts;
            }
        }

        if (cand < 16) {
            idx = cand;
            array[idx].alloc(reqCycle);
            array[idx].lastPos = pos;
            array[idx].ts = timestamp++;
            tag[idx] = pageAddr;
        }
        DBG("%s: MISS alloc idx %d", name.c_str(), idx);
    } else {  // entry hit
        profPageHits.inc();
        Entry& e = array[idx];
        array[idx].ts = timestamp++;
        DBG("%s: PAGE HIT idx %d", name.c_str(), idx);

        // 1. Did we prefetch-hit?
        bool shortPrefetch = false;
        if (e.valid[pos]) {
            uint64_t pfRespCycle = e.times[pos].respCycle;
            shortPrefetch = pfRespCycle > respCycle;
            e.valid[pos] = false;  // close, will help with long-lived transactions
            respCycle = MAX(pfRespCycle, respCycle);
            e.lastCycle = MAX(respCycle, e.lastCycle);
            profHits.inc();
            if (shortPrefetch) profShortHits.inc();
            DBG("%s: pos %d prefetched on %ld, pf resp %ld, demand resp %ld, short %d", name.c_str(), pos, e.times[pos].startCycle, pfRespCycle, respCycle, shortPrefetch);
        }

        // 2. Update predictors, issue prefetches
        int32_t stride = pos - e.lastPos;
        DBG("%s: pos %d lastPos %d lastLastPost %d e.stride %d", name.c_str(), pos, e.lastPos, e.lastLastPos, e.stride);
        if (e.stride == stride) {
            e.conf.inc();
            if (e.conf.pred()) {  // do prefetches
                int32_t fetchDepth = (e.lastPrefetchPos - e.lastPos)/stride;
                uint32_t prefetchPos = e.lastPrefetchPos + stride;
                if (fetchDepth < 1) {
                    prefetchPos = pos + stride;
                    fetchDepth = 1;
                }
                DBG("%s: pos %d stride %d conf %d lastPrefetchPos %d prefetchPos %d fetchDepth %d", name.c_str(), pos, stride, e.conf.counter(), e.lastPrefetchPos, prefetchPos, fetchDepth);

                if (prefetchPos < 64 && !e.valid[prefetchPos]) {
                    MESIState state = I;
                    MemReq pfReq = {req.lineAddr + prefetchPos - pos, GETS, req.childId, &state, reqCycle, req.childLock, state, req.srcId, MemReq::PREFETCH, req.previous_instructions, req.coreId, MemReq::PREFETCH_TRACE};
                    uint64_t pfRespCycle = parents[parentId]->access(pfReq);  // FIXME, might segfault
                    e.valid[prefetchPos] = true;
                    e.times[prefetchPos].fill(reqCycle, pfRespCycle);
                    profPrefetches.inc();

                    if (shortPrefetch && fetchDepth < 8 && prefetchPos + stride < 64 && !e.valid[prefetchPos + stride]) {
                        prefetchPos += stride;
                        pfReq.lineAddr += stride;
                        pfRespCycle = parents[parentId]->access(pfReq);
                        e.valid[prefetchPos] = true;
                        e.times[prefetchPos].fill(reqCycle, pfRespCycle);
                        profPrefetches.inc();
                        profDoublePrefetches.inc();
                    }
                    e.lastPrefetchPos = prefetchPos;
                    assert(state == I);  // prefetch access should not give us any permissions
                }
            } else {
                profLowConfAccs.inc();
            }
        } else {
            e.conf.dec();
            // See if we need to switch strides
            if (!e.conf.pred()) {
                int32_t lastStride = e.lastPos - e.lastLastPos;

                if (stride && stride != e.stride && stride == lastStride) {
                    e.conf.reset();
                    e.stride = stride;
                    profStrideSwitches.inc();
                }
            }
            e.lastPrefetchPos = pos;
        }

        e.lastLastPos = e.lastPos;
        e.lastPos = pos;
    }

    req.childId = origChildId;
    return respCycle;
}

// nop for now; do we need to invalidate our own state?
uint64_t StreamPrefetcher::invalidate(const InvReq& req) {
    for(int i = 0; i < numchildren; i++){
        children[i]->invalidate(req);
    }
    //return child->invalidate(req);
    return 0;
}

void AMPMPrefetcher::setParents(uint32_t _childId, const g_vector<MemObject*>& _parents, Network* network) {
    childId = _childId;
    printf("----[AMPMPrefetcher] Parents: %d\n",_parents.size());
    //if (parents.size() != 1) panic("Must have one parent");
    if (network) panic("Network not handled");
    //parent = parents[0];
    numparents = _parents.size();
    for(int i = 0; i < numparents; i++){
        parents.push_back(_parents[i]);
    }
}

void AMPMPrefetcher::setChildren(const g_vector<BaseCache*>& _children, Network* network) {
    printf("---- Chidren: %d\n", _children.size());
    if (_children.size() != 1) panic("Must have one children");
    if (network) panic("Network not handled");
    numchildren = _children.size();
    for(int i = 0; i < numchildren; i++){
        children.push_back(_children[i]);
    }
    //child = _children[0];
}

void AMPMPrefetcher::initStats(AggregateStat* parentStat) {
    AggregateStat* s = new AggregateStat();
    s->init(name.c_str(), "Prefetcher stats");
    profAccesses.init("acc", "Accesses"); s->append(&profAccesses);
    profPrefetches.init("pf", "Issued prefetches"); s->append(&profPrefetches);
    profDoublePrefetches.init("dpf", "Issued double prefetches"); s->append(&profDoublePrefetches);
    profPageHits.init("pghit", "Page/entry hit"); s->append(&profPageHits);
    profHits.init("hit", "Prefetch buffer hits, short and full"); s->append(&profHits);
    profShortHits.init("shortHit", "Prefetch buffer short hits"); s->append(&profShortHits);
    profStrideSwitches.init("strideSwitches", "Predicted stride switches"); s->append(&profStrideSwitches);
    profLowConfAccs.init("lcAccs", "Low-confidence accesses with no prefetches"); s->append(&profLowConfAccs);
    parentStat->append(s);
}


uint64_t AMPMPrefetcher::invalidate(const InvReq& req) {
    for(int i = 0; i < numchildren; i++){
        children[i]->invalidate(req);
    }
    //return child->invalidate(req);
    return 0;
}

// AMPM Prefetcher : https://github.com/charlab/dpc2/blob/master/dpc2sim/example_prefetchers/ampm_lite_prefetcher.c
 // void l2_prefetcher_operate(unsigned long long int addr, unsigned long long int ip, int cache_hit)
uint64_t AMPMPrefetcher::access(MemReq& req) {
    // uncomment this line to see all the information available to make prefetch decisions
    //printf("(0x%llx 0x%llx %d %d %d) ", addr, ip, cache_hit, get_l2_read_queue_occupancy(0), get_l2_mshr_occupancy(0));
    uint32_t origChildId = req.childId;
    req.childId = childId;

    uint32_t parentId = getParentId(req.lineAddr);
    if (req.type != GETS) return parents[parentId]->access(req); //other reqs ignored, including stores

    profAccesses.inc();

    uint64_t reqCycle = req.cycle;
    uint64_t respCycle = parents[parentId]->access(req);

    //Address pageAddr = req.lineAddr >> 6;
    Address page = req.lineAddr >> 6;
    uint32_t page_offset = req.lineAddr & (64-1);
    DBG("%s: 0x%lx page %lx pos %d", name.c_str(), req.lineAddr, pageAddr, pos);

    // check to see if we have a page hit
    int page_index = -1;
    int i;
    for(i=0; i<AMPM_PAGE_COUNT; i++){
        if(ampm_pages[i].page == page){
            page_index = i;
            break;
        }
    }

    if(page_index == -1){
        // the page was not found, so we must replace an old page with this new page
        // find the oldest page
        int lru_index = 0;
        unsigned long long int lru_cycle = ampm_pages[lru_index].lru;
        int i;
        for(i=0; i<AMPM_PAGE_COUNT; i++){
            if(ampm_pages[i].lru < lru_cycle){
                lru_index = i;
                lru_cycle = ampm_pages[lru_index].lru;
            }
        }
        page_index = lru_index;

        // reset the oldest page
        ampm_pages[page_index].page = page;
        for(i=0; i<64; i++){
            ampm_pages[page_index].access_map[i] = 0;
            ampm_pages[page_index].pf_map[i] = 0;
        }
    }

    // update LRU
    ampm_pages[page_index].lru = reqCycle; //get_current_cycle(0);

    // mark the access map
    ampm_pages[page_index].access_map[page_offset] = 1;

    // positive prefetching
    int count_prefetches = 0;
    for(i=1; i<=16; i++){
        int check_index1 = page_offset - i;
        int check_index2 = page_offset - 2*i;
        int pf_index = page_offset + i;

        if(check_index2 < 0) break;
        if(pf_index > 63) break;

        if(count_prefetches >= PREFETCH_DEGREE) break;

        // don't prefetch something that's already been demand accessed
        if(ampm_pages[page_index].access_map[pf_index] == 1) continue;

        // don't prefetch something that's alrady been prefetched
        if(ampm_pages[page_index].pf_map[pf_index] == 1) continue;



        if((ampm_pages[page_index].access_map[check_index1]==1) && (ampm_pages[page_index].access_map[check_index2]==1)){
            // we found the stride repeated twice, so issue a prefetch

            //unsigned long long int pf_address = (page<<12)+(pf_index<<6);
            unsigned long long int pf_address = (page<<6)+(pf_index);

                MESIState state = I;
                MemReq pfReq = {pf_address, GETS, req.childId, &state, reqCycle, req.childLock, state, req.srcId, MemReq::PREFETCH, req.previous_instructions, req.coreId, MemReq::PREFETCH_TRACE};
                uint64_t pfRespCycle = parents[parentId]->access(pfReq);  // FIXME, might segfault
                ampm_pages[page_index].pf_map[pf_index] = 1;
                count_prefetches++;
                assert(state == I);  // prefetch access should not give us any permissions
                profPrefetches.inc();
        }
    }

    // negative prefetching
    count_prefetches = 0;
    for(i=1; i<=16; i++)
    {
        int check_index1 = page_offset + i;
        int check_index2 = page_offset + 2*i;
        int pf_index = page_offset - i;

        if(check_index2 > 63) break;
        if(pf_index < 0) break;
        if(count_prefetches >= PREFETCH_DEGREE) break;

        // don't prefetch something that's already been demand accessed
        if(ampm_pages[page_index].access_map[pf_index] == 1) continue;

        // don't prefetch something that's alrady been prefetched
        if(ampm_pages[page_index].pf_map[pf_index] == 1) continue;

        if((ampm_pages[page_index].access_map[check_index1]==1) &&
                (ampm_pages[page_index].access_map[check_index2]==1)){
            // we found the stride repeated twice, so issue a prefetch

            //unsigned long long int pf_address = (page<<12)+(pf_index<<6);
            unsigned long long int pf_address = (page<<6)+(pf_index);

            MESIState state = I;
            MemReq pfReq = {pf_address, GETS, req.childId, &state, reqCycle, req.childLock, state, req.srcId, MemReq::PREFETCH, req.previous_instructions, req.coreId, MemReq::PREFETCH_TRACE};
            uint64_t pfRespCycle = parents[parentId]->access(pfReq);  // FIXME, might segfault
            profPrefetches.inc();
            ampm_pages[page_index].pf_map[pf_index] = 1;
            count_prefetches++;
        }
    }
    return respCycle;
}






