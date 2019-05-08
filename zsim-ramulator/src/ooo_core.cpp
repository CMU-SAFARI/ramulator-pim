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

#include "ooo_core.h"
#include <algorithm>
#include <queue>
#include <string>
#include "bithacks.h"
#include "decoder.h"
#include "filter_cache.h"
#include "zsim.h"

/* Uncomment to induce backpressure to the IW when the load/store buffers fill up. In theory, more detailed,
 * but sometimes much slower (as it relies on range poisoning in the IW, potentially O(n^2)), and in practice
 * makes a negligible difference (ROB backpressures).
 */
//#define LSU_IW_BACKPRESSURE

#define DEBUG_MSG(args...)
//#define DEBUG_MSG(args...) info(args)

// Core parameters
// TODO(dsm): Make OOOCore templated, subsuming these

// Stages --- more or less matched to Westmere, but have not seen detailed pipe diagrams anywhare
#define FETCH_STAGE 1
#define DECODE_STAGE 4  // NOTE: Decoder adds predecode delays to decode
#define ISSUE_STAGE 7
#define DISPATCH_STAGE 13  // RAT + ROB + RS, each is easily 2 cycles

#define L1D_LAT 4  // fixed, and FilterCache does not include L1 delay
#define FETCH_BYTES_PER_CYCLE 16
#define ISSUES_PER_CYCLE 4
#define RF_READS_PER_CYCLE 3

OOOCore::OOOCore(FilterCache* _l1i, FilterCache* _l1d, g_string& _name) : Core(_name), l1i(_l1i), l1d(_l1d), cRec(0, _name) {
    decodeCycle = DECODE_STAGE;  // allow subtracting from it
    curCycle = 0;
    phaseEndCycle = zinfo->phaseLength;

    pim_trace = false; // LOIS we get the trace in the MC, not in the core
    previous_instr = 0;
    ptrace = false;
    print_trace = false;
    only_offload = false;
    total_missing_instructions = 0;
    function_offload_instructions = 0;
    // LOIS: Get the coreIdx from the name
    g_string delim = "-";
    auto start = 0U;
    auto end = name.find(delim);
    while (end != std::string::npos)
    {
      start = end + delim.length();
      end = name.find(delim, start);
    }

    // LOIS: We update this values in each basic block
    coreIdx = atoi((name.substr(start, end)).c_str());
    threadIdx = coreIdx; 

    for (uint32_t i = 0; i < MAX_REGISTERS; i++) {
        regScoreboard[i] = 0;
    }
    prevBbl = nullptr;

    lastStoreCommitCycle = 0;
    lastStoreAddrCommitCycle = 0;
    curCycleRFReads = 0;
    curCycleIssuedUops = 0;
    branchPc = 0;
    
    instrs = uops = bbls = approxInstrs = mispredBranches = 0;

    inter_instrs = 0; 
    offload_code = 0;

    for (uint32_t i = 0; i < FWD_ENTRIES; i++) fwdArray[i].set((Address)(-1L), 0);
}

OOOCore::OOOCore(FilterCache* _l1i, FilterCache* _l1d, g_string& _name, bool _only_offload) : Core(_name), l1i(_l1i), l1d(_l1d), cRec(0, _name) {
    decodeCycle = DECODE_STAGE;  // allow subtracting from it
    curCycle = 0;
    phaseEndCycle = zinfo->phaseLength;

    pim_trace = false; // LOIS we get the trace in the MC, not in the core
    previous_instr = 0;
    ptrace = false;
    print_trace = false;
    only_offload = _only_offload; // only difference with the previous constructor
    function_offload_instructions = 0;
    total_missing_instructions = 0;
    printf("Only offload: %d\n", (int)only_offload);
    // LOIS: Get the coreIdx from the name
    g_string delim = "-";
    auto start = 0U;
    auto end = name.find(delim);
    while (end != std::string::npos)
    {
      start = end + delim.length();
      end = name.find(delim, start);
    }

    // LOIS: We update this values in each basic block
    coreIdx = atoi((name.substr(start, end)).c_str());
    threadIdx = coreIdx; 

    for (uint32_t i = 0; i < MAX_REGISTERS; i++) {
        regScoreboard[i] = 0;
    }
    prevBbl = nullptr;

    lastStoreCommitCycle = 0;
    lastStoreAddrCommitCycle = 0;
    curCycleRFReads = 0;
    curCycleIssuedUops = 0;
    branchPc = 0;
    
    instrs = uops = bbls = approxInstrs = mispredBranches = 0;

    inter_instrs = 0; 
    offload_code = 0;

    for (uint32_t i = 0; i < FWD_ENTRIES; i++) fwdArray[i].set((Address)(-1L), 0);
}

///////////////////////////////////////////////////////////////////
// Constructor for generating traces in the core, and not in the MC
// ////////////////////////////////////////////////////////////////
std::mutex OOOCore::mtx_traces;
bool OOOCore::file_created = false;

OOOCore::OOOCore(FilterCache* _l1i, FilterCache* _l1d, g_string& _name, bool only_offload_, bool instr_trace_, g_string& _outFile, bool _merge_hostTraces) : Core(_name), l1i(_l1i), l1d(_l1d), cRec(0, _name) {
    printf("--> Generating traces in the core\n");
    decodeCycle = DECODE_STAGE;  // allow subtracting from it
    curCycle = 0;
    phaseEndCycle = zinfo->phaseLength;

    // Options for Generating traces
    pim_trace = true;
    previous_instr = 0;
    total_missing_instructions = 0;
    only_offload = only_offload_;
    if(!only_offload){
      ptrace = true;
      print_trace = true;
    } else {
      ptrace = false;
      print_trace = false;
    }


    instr_trace = instr_trace_;
    offload_instrs = 0;
    function_offload_instructions = 0;

    // LOIS: Get the coreIdx from the name
    g_string delim = "-";
    auto start = 0U;
    auto end = name.find(delim);
    while (end != std::string::npos)
    {
      start = end + delim.length();
      end = name.find(delim, start);
    }
    coreIdx = atoi((name.substr(start, end)).c_str());
    
    //LOIS
    merge_hostTraces = _merge_hostTraces;
    if(merge_hostTraces){
      mtx_traces.lock();
      if(!file_created) {
        tracefile.open (_outFile.c_str());
        file_created = true;
      } else {
        tracefile.open (_outFile.c_str(), ios::app);
      }
      mtx_traces.unlock();
    } else {
      // GERALDO
      string tmp(_outFile.c_str());
      string tmp2(".");
      string to_open = tmp + tmp2 + std::to_string(coreIdx);
      printf("--> Conf File: %s\n", to_open.c_str());
      tracefile.open(to_open.c_str());
    }
        
    for (uint32_t i = 0; i < MAX_REGISTERS; i++) {
        regScoreboard[i] = 0;
    }
    prevBbl = nullptr;

    lastStoreCommitCycle = 0;
    lastStoreAddrCommitCycle = 0;
    curCycleRFReads = 0;
    curCycleIssuedUops = 0;
    branchPc = 0;
    
    instrs = uops = bbls = approxInstrs = mispredBranches = 0;

    inter_instrs = 0; 
    offload_code = 0;

    for (uint32_t i = 0; i < FWD_ENTRIES; i++) fwdArray[i].set((Address)(-1L), 0);
}


void OOOCore::initStats(AggregateStat* parentStat) {
    AggregateStat* coreStat = new AggregateStat();
    coreStat->init(name.c_str(), "Core stats");

    auto x = [this]() { return cRec.getUnhaltedCycles(curCycle); };
    LambdaStat<decltype(x)>* cyclesStat = new LambdaStat<decltype(x)>(x);
    cyclesStat->init("cycles", "Simulated unhalted cycles");

    auto y = [this]() { return cRec.getContentionCycles(); };
    LambdaStat<decltype(y)>* cCyclesStat = new LambdaStat<decltype(y)>(y);
    cCyclesStat->init("cCycles", "Cycles due to contention stalls");

    ProxyStat* instrsStat = new ProxyStat();
    instrsStat->init("instrs", "Simulated instructions", &instrs);
    ProxyStat* uopsStat = new ProxyStat();
    uopsStat->init("uops", "Retired micro-ops", &uops);
    ProxyStat* bblsStat = new ProxyStat();
    bblsStat->init("bbls", "Basic blocks", &bbls);
    ProxyStat* approxInstrsStat = new ProxyStat();
    approxInstrsStat->init("approxInstrs", "Instrs with approx uop decoding", &approxInstrs);
    ProxyStat* mispredBranchesStat = new ProxyStat();
    mispredBranchesStat->init("mispredBranches", "Mispredicted branches", &mispredBranches);

    coreStat->append(cyclesStat);
    coreStat->append(cCyclesStat);
    coreStat->append(instrsStat);
    coreStat->append(uopsStat);
    coreStat->append(bblsStat);
    coreStat->append(approxInstrsStat);
    coreStat->append(mispredBranchesStat);

#ifdef OOO_STALL_STATS
    profFetchStalls.init("fetchStalls",  "Fetch stalls");  coreStat->append(&profFetchStalls);
    profDecodeStalls.init("decodeStalls", "Decode stalls"); coreStat->append(&profDecodeStalls);
    profIssueStalls.init("issueStalls",  "Issue stalls");  coreStat->append(&profIssueStalls);
#endif

    parentStat->append(coreStat);
}

uint64_t OOOCore::getInstrs() const {return instrs;}
uint64_t OOOCore::getOffloadInstrs() const {return offload_instrs;}
uint64_t OOOCore::getPhaseCycles() const {return curCycle % zinfo->phaseLength;}

void OOOCore::contextSwitch(int32_t gid) {
    if (gid == -1) {
        // Do not execute previous BBL, as we were context-switched
        prevBbl = nullptr;

        // Invalidate virtually-addressed filter caches
        l1i->contextSwitch();
        l1d->contextSwitch();
    }
}


//InstrFuncPtrs OOOCore::GetFuncPtrs() {return {LoadFunc, StoreFunc, BblFunc, BranchFunc, PredLoadFunc, PredStoreFunc, FPTR_ANALYSIS, {0}};}
InstrFuncPtrs OOOCore::GetFuncPtrs() {
    return {LoadFunc, StoreFunc, BblFunc, BranchFunc, PredLoadFunc, PredStoreFunc, OffloadBegin, OffloadEnd, FPTR_ANALYSIS, {0} };
}

inline void OOOCore::load(Address addr, uint32_t size) {
    loadAddrs[loads] = addr;
    loadSizes[loads] = size;
    loads++;
}

void OOOCore::store(Address addr, uint32_t size) {
    storeAddrs[stores] = addr;
    storeSizes[stores] = size;
    stores++;
}

// Predicated loads and stores call this function, gets recorded as a 0-cycle op.
// Predication is rare enough that we don't need to model it perfectly to be accurate (i.e. the uops still execute, retire, etc), but this is needed for correctness.
void OOOCore::predFalseMemOp() {
    // I'm going to go out on a limb and assume just loads are predicated (this will not fail silently if it's a store)
    loadAddrs[loads] = -1L;
    loadSizes[loads] = 0;
    loads++;
}

void OOOCore::branch(Address pc, bool taken, Address takenNpc, Address notTakenNpc) {
    branchPc = pc;
    branchTaken = taken;
    branchTakenNpc = takenNpc;
    branchNotTakenNpc = notTakenNpc;
}

// Call the memory controller
// LOIS: Offload code
// Deprecated
inline void OOOCore::bbl_offload_mc(Address bblAddr, BblInfo* bblInfo) {
    /* Simulate execution of previous BBL */
    //uint32_t bblInstrs = prevBbl->instrs;
    DynBbl* bbl = &(prevBbl->oooBbl[0]);

    uint32_t loadIdx = 0;
    uint32_t storeIdx = 0;

    // Run dispatch/IW
    for (uint32_t i = 0; i < bbl->uops; i++) {
      DynUop* uop = &(bbl->uop[i]);
      switch (uop->type) {
        case UOP_LOAD:
          {
            Address addr = loadAddrs[loadIdx++];
            offloadInfo offData;
            offData.ld_addr = addr;
            offData.st_addr = 0;
            offData.inter_instrs = inter_instrs;
            offData.coreIdx = coreIdx;
            l1d->offload(offData);// 0 because there is not write address
            inter_instrs = 0;
            break;
          }
        case UOP_STORE:
          {
            Address addr = storeAddrs[storeIdx++];
            offloadInfo offData;
            offData.ld_addr = 0;
            offData.st_addr = addr;
            offData.inter_instrs = inter_instrs;
            offData.coreIdx = coreIdx;
            l1d->offload(offData);
            inter_instrs = 0;
            break;
          }
        default:
          {
            inter_instrs++;
            break;
          }
      }
    }
    //assert_msg(loadIdx == loads, "%s: loadIdx(%d) != loads (%d)", name.c_str(), loadIdx, loads);
    //assert_msg(storeIdx == stores, "%s: storeIdx(%d) != stores (%d)", name.c_str(), storeIdx, stores);
    loads = stores = 0;
}

/*
inline void OOOCore::bbl_offload() {
  //print_trace = true;
  //assert(!(regular_traces && pim_traces)); // TODO: no regular memory traces from the core 
  //if((!pim_traces) && only_offload) {
    //bbl_offload_mc(bblAddr, bblInfo); // host traces, only in offloaded functions
  if(only_offload){
    print_trace = true;
    if((!pim_trace) && only_offload) {
     offloadInfo offData;
     offData.msg = OFFLOADING;
     offData.coreIdx = coreIdx;
     offData.threadIdx = threadIdx;
     offData.content = ptrace?(1):(-1); // enter in the offloading funtion
     l1d->offload(offData);// LOIS: just to indicate to the memory controller to write in the trace
    }
  }
}
*/

inline void OOOCore::bbl(Address bblAddr, BblInfo* bblInfo) {
    if (!prevBbl) {
        // This is the 1st BBL since scheduled, nothing to simulate
        prevBbl = bblInfo;
        // Kill lingering ops from previous BBL
        loads = stores = 0;
        return;
    }

    if(get_offload_code()>0){
	    print_trace = true;
	    if((!pim_trace) && only_offload) {
		    offloadInfo offData;
		    offData.msg = OFFLOADING;
		    offData.coreIdx = coreIdx;
		    offData.threadIdx = threadIdx;
		    offData.content = print_trace?1:0; // enter in the offloading funtion
		    l1d->offload(offData);// LOIS: just to indicate to the memory controller to write in the trace
	    }
    }


    /* Simulate execution of previous BBL */

    uint32_t bblInstrs = prevBbl->instrs;
    DynBbl* bbl = &(prevBbl->oooBbl[0]);
    prevBbl = bblInfo;

    uint32_t loadIdx = 0;
    uint32_t storeIdx = 0;

    uint32_t prevDecCycle = 0;
    uint64_t lastCommitCycle = 0;  // used to find misprediction penalty

    // Run dispatch/IW
    for (uint32_t i = 0; i < bbl->uops; i++) {
        DynUop* uop = &(bbl->uop[i]);

        // Decode stalls
        uint32_t decDiff = uop->decCycle - prevDecCycle;
        decodeCycle = MAX(decodeCycle + decDiff, uopQueue.minAllocCycle());
        if (decodeCycle > curCycle) {
            //info("Decode stall %ld %ld | %d %d", decodeCycle, curCycle, uop->decCycle, prevDecCycle);
            uint32_t cdDiff = decodeCycle - curCycle;
#ifdef OOO_STALL_STATS
            profDecodeStalls.inc(cdDiff);
#endif
            curCycleIssuedUops = 0;
            curCycleRFReads = 0;
            for (uint32_t i = 0; i < cdDiff; i++) insWindow.advancePos(curCycle);
        }
        prevDecCycle = uop->decCycle;
        uopQueue.markLeave(curCycle);

        // Implement issue width limit --- we can only issue 4 uops/cycle
        if (curCycleIssuedUops >= ISSUES_PER_CYCLE) {
#ifdef OOO_STALL_STATS
            profIssueStalls.inc();
#endif
            // info("Advancing due to uop issue width");
            curCycleIssuedUops = 0;
            curCycleRFReads = 0;
            insWindow.advancePos(curCycle);
        }
        curCycleIssuedUops++;

        // Kill dependences on invalid register
        // Using curCycle saves us two unpredictable branches in the RF read stalls code
        regScoreboard[0] = curCycle;

        uint64_t c0 = regScoreboard[uop->rs[0]];
        uint64_t c1 = regScoreboard[uop->rs[1]];

        // RF read stalls
        // if srcs are not available at issue time, we have to go thru the RF
        curCycleRFReads += ((c0 < curCycle)? 1 : 0) + ((c1 < curCycle)? 1 : 0);
        if (curCycleRFReads > RF_READS_PER_CYCLE) {
            curCycleRFReads -= RF_READS_PER_CYCLE;
            curCycleIssuedUops = 0;  // or 1? that's probably a 2nd-order detail
            insWindow.advancePos(curCycle);
        }

        uint64_t c2 = rob.minAllocCycle();
        uint64_t c3 = curCycle;

        uint64_t cOps = MAX(c0, c1);

        // Model RAT + ROB + RS delay between issue and dispatch
        uint64_t dispatchCycle = MAX(cOps, MAX(c2, c3) + (DISPATCH_STAGE - ISSUE_STAGE));

        // info("IW 0x%lx %d %ld %ld %x", bblAddr, i, c2, dispatchCycle, uop->portMask);
        // NOTE: Schedule can adjust both cur and dispatch cycles
        insWindow.schedule(curCycle, dispatchCycle, uop->portMask, uop->extraSlots);

        // If we have advanced, we need to reset the curCycle counters
        if (curCycle > c3) {
            curCycleIssuedUops = 0;
            curCycleRFReads = 0;
        }

        uint64_t commitCycle;

        // LSU simulation
        // NOTE: Ever-so-slightly faster than if-else if-else if-else
	//
			
	if(ptrace) {
	  offload_instrs++; // total
      function_offload_instructions++; // per function (reseted at the end of the function)
    }
	//printf("Instr: %ld\n", instrs);
	switch (uop->type) {
            case UOP_GENERAL:
                commitCycle = dispatchCycle + uop->lat;
                if(ptrace) previous_instr++;//LOIS
                break;

            case UOP_LOAD:
                {
                    // dispatchCycle = MAX(loadQueue.minAllocCycle(), dispatchCycle);
                    uint64_t lqCycle = loadQueue.minAllocCycle();
                    if (lqCycle > dispatchCycle) {
#ifdef LSU_IW_BACKPRESSURE
                        insWindow.poisonRange(curCycle, lqCycle, 0x4 /*PORT_2, loads*/);
#endif
                        dispatchCycle = lqCycle;
                    }

                    // Wait for all previous store addresses to be resolved
                    dispatchCycle = MAX(lastStoreAddrCommitCycle+1, dispatchCycle);

                    Address addr = loadAddrs[loadIdx];
                    uint32_t size = loadSizes[loadIdx];
                    loadIdx++;
                    uint64_t reqSatisfiedCycle = dispatchCycle;
                    if (addr != ((Address)-1L)) {
                        // LOIS
                        if( pim_trace && ptrace ){
                            //THREAD_ID PROCESSOR_ID  CYCLE_NUM TYPE  ADDRESS SIZE
                            if(merge_hostTraces){
                              mtx_traces.lock();
                              tracefile << threadIdx << " "<< coreIdx << " " << previous_instr << " L " << addr << " " <<  size << std::endl;
                              mtx_traces.unlock();
                            } else {
                              tracefile << threadIdx << " "<< coreIdx << " " << previous_instr << " L " << addr << " " <<  size << std::endl;
                            }
                        } else {
                            // NOtify the MC about the number of instructions 
                            if(!pim_trace && ptrace) {
                                offloadInfo offData;
                                offData.msg = INSTR_COUNT; // enter in the offloading funtion
                                offData.coreIdx = coreIdx;
                                offData.threadIdx = threadIdx;
                                offData.inter_instrs = previous_instr +1; // include cache hit
                                l1d->offload(offData);// LOIS: just to indicate to the memory controller to the number of instructions executed before
                                //printf("offload_instructions: %lld\n", (long long int)offload_instrs);
                            } else total_missing_instructions += previous_instr;
                        }
                        //printf("MIssed instructions: %lld", (long long int)total_missing_instructions);
                        previous_instr = 0;
                        reqSatisfiedCycle = l1d->load(addr, dispatchCycle, instrs) + L1D_LAT;
                        cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);
                    }

                    // Enforce st-ld forwarding
                    uint32_t fwdIdx = (addr>>2) & (FWD_ENTRIES-1);
                    if (fwdArray[fwdIdx].addr == addr) {
                        // info("0x%lx FWD %ld %ld", addr, reqSatisfiedCycle, fwdArray[fwdIdx].storeCycle);
                        /* Take the MAX (see FilterCache's code) Our fwdArray
                         * imposes more stringent timing constraints than the
                         * l1d, b/c FilterCache does not change the line's
                         * availCycle on a store. This allows FilterCache to
                         * track per-line, not per-word availCycles.
                         */
                        reqSatisfiedCycle = MAX(reqSatisfiedCycle, fwdArray[fwdIdx].storeCycle);
                    }

                    commitCycle = reqSatisfiedCycle;
                    loadQueue.markRetire(commitCycle);
                }
                break;

            case UOP_STORE:
                {
                    // dispatchCycle = MAX(storeQueue.minAllocCycle(), dispatchCycle);
                    uint64_t sqCycle = storeQueue.minAllocCycle();
                    if (sqCycle > dispatchCycle) {
#ifdef LSU_IW_BACKPRESSURE
                        insWindow.poisonRange(curCycle, sqCycle, 0x10 /*PORT_4, stores*/);
#endif
                        dispatchCycle = sqCycle;
                    }

                    // Wait for all previous store addresses to be resolved (not just ours :))
                    dispatchCycle = MAX(lastStoreAddrCommitCycle+1, dispatchCycle);

                    Address addr = storeAddrs[storeIdx];
                    uint32_t size = storeSizes[storeIdx];
                    storeIdx++;
                    // LOIS
                    if( pim_trace &&  ptrace){
                        //THREAD_ID PROCESSOR_ID  CYCLE_NUM TYPE  ADDRESS SIZE
                        if(merge_hostTraces){
                          mtx_traces.lock();
                          tracefile << threadIdx << " " << coreIdx << " " << previous_instr << " S " << addr << " "<< size << std::endl;
                          mtx_traces.unlock();
                        } else {
                          tracefile << threadIdx << " " << coreIdx << " " << previous_instr << " S " << addr << " "<< size << std::endl;
                        }   
                    } else {
                        // NOtify the MC about the number of instructions 
                        if(!pim_trace && ptrace) {
                            offloadInfo offData;
                            offData.msg = INSTR_COUNT; // enter in the offloading funtion
                            offData.coreIdx = coreIdx;
                            offData.threadIdx = threadIdx;
                            offData.inter_instrs = previous_instr +1 ; //include cache hits
                            l1d->offload(offData);// LOIS: just to indicate to the memory controller the number of instructions executed since the last memory instruction
                        } else total_missing_instructions += previous_instr;
                    }
                    previous_instr = 0;
                    uint64_t reqSatisfiedCycle = l1d->store(addr, dispatchCycle, instrs) + L1D_LAT;
                    cRec.record(curCycle, dispatchCycle, reqSatisfiedCycle);

                    // Fill the forwarding table
                    fwdArray[(addr>>2) & (FWD_ENTRIES-1)].set(addr, reqSatisfiedCycle);

                    commitCycle = reqSatisfiedCycle;
                    lastStoreCommitCycle = MAX(lastStoreCommitCycle, reqSatisfiedCycle);
                    storeQueue.markRetire(commitCycle);
                }
                break;

            case UOP_STORE_ADDR:
                commitCycle = dispatchCycle + uop->lat;
                lastStoreAddrCommitCycle = MAX(lastStoreAddrCommitCycle, commitCycle);
                if(ptrace) previous_instr++;//LOIS
                break;

            //case UOP_FENCE:  //make gcc happy
            default:
                assert((UopType) uop->type == UOP_FENCE);
                if(ptrace) previous_instr++;//LOIS
                commitCycle = dispatchCycle + uop->lat;
                // info("%d %ld %ld", uop->lat, lastStoreAddrCommitCycle, lastStoreCommitCycle);
                // force future load serialization
                lastStoreAddrCommitCycle = MAX(commitCycle, MAX(lastStoreAddrCommitCycle, lastStoreCommitCycle + uop->lat));
                // info("%d %ld %ld X", uop->lat, lastStoreAddrCommitCycle, lastStoreCommitCycle);
        }

        // Mark retire at ROB
        rob.markRetire(commitCycle);

        // Record dependences
        regScoreboard[uop->rd[0]] = commitCycle;
        regScoreboard[uop->rd[1]] = commitCycle;

        lastCommitCycle = commitCycle;

        //info("0x%lx %3d [%3d %3d] -> [%3d %3d]  %8ld %8ld %8ld %8ld", bbl->addr, i, uop->rs[0], uop->rs[1], uop->rd[0], uop->rd[1], decCycle, c3, dispatchCycle, commitCycle);
    }

    instrs += bblInstrs;
    uops += bbl->uops;
    bbls++;
    approxInstrs += bbl->approxInstrs;

#ifdef BBL_PROFILING
    if (approxInstrs) Decoder::profileBbl(bbl->bblIdx);
#endif

    // Check full match between expected and actual mem ops
    // If these assertions fail, most likely, something's off in the decoder
   // assert_msg(loadIdx == loads, "%s: loadIdx(%d) != loads (%d)", name.c_str(), loadIdx, loads);
   // assert_msg(storeIdx == stores, "%s: storeIdx(%d) != stores (%d)", name.c_str(), storeIdx, stores);
    loads = stores = 0;


    /* Simulate frontend for branch pred + fetch of this BBL
     *
     * NOTE: We assume that the instruction length predecoder and the IQ are
     * weak enough that they can't hide any ifetch or bpred stalls. In fact,
     * predecoder stalls are incorporated in the decode stall component (see
     * decoder.cpp). So here, we compute fetchCycle, then use it to adjust
     * decodeCycle.
     */

    // Model fetch-decode delay (fixed, weak predec/IQ assumption)
    uint64_t fetchCycle = decodeCycle - (DECODE_STAGE - FETCH_STAGE);
    uint32_t lineSize = 1 << lineBits;

    // Simulate branch prediction
    if (branchPc && !branchPred.predict(branchPc, branchTaken)) {
        mispredBranches++;

        /* Simulate wrong-path fetches
         *
         * This is not for a latency reason, but sometimes it increases fetched
         * code footprint and L1I MPKI significantly. Also, we assume a perfect
         * BTB here: we always have the right address to missfetch on, and we
         * never need resteering.
         *
         * NOTE: Resteering due to BTB misses is done at the BAC unit, is
         * relatively rare, and carries an 8-cycle penalty, which should be
         * partially hidden if the branch is predicted correctly --- so we
         * don't simulate it.
         *
         * Since we don't have a BTB, we just assume the next branch is not
         * taken. With a typical branch mispred penalty of 17 cycles, we
         * typically fetch 3-4 lines in advance (16B/cycle). This sets a higher
         * limit, which can happen with branches that take a long time to
         * resolve (because e.g., they depend on a load). To set this upper
         * bound, assume a completely backpressured IQ (18 instrs), uop queue
         * (28 uops), IW (36 uops), and 16B instr length predecoder buffer. At
         * ~3.5 bytes/instr, 1.2 uops/instr, this is about 5 64-byte lines.
         */

        // info("Mispredicted branch, %ld %ld %ld | %ld %ld", decodeCycle, curCycle, lastCommitCycle,
        //         lastCommitCycle-decodeCycle, lastCommitCycle-curCycle);
        Address wrongPathAddr = branchTaken? branchNotTakenNpc : branchTakenNpc;
        uint64_t reqCycle = fetchCycle;
	for (uint32_t i = 0; i < 5*64/lineSize; i++) {
	    uint64_t fetchLat = l1i->load(wrongPathAddr + lineSize*i, curCycle, instrs) - curCycle;
	    cRec.record(curCycle, curCycle, curCycle + fetchLat);
	    uint64_t respCycle = reqCycle + fetchLat;
	    if (respCycle > lastCommitCycle) {
		break;
	    }
	    // Model fetch throughput limit
	    reqCycle = respCycle + lineSize/FETCH_BYTES_PER_CYCLE;
	    // LOIS
	    if( pim_trace && instr_trace && ptrace){
		//THREAD_ID PROCESSOR_ID  CYCLE_NUM TYPE  ADDRESS SIZE
        if(merge_hostTraces){
          mtx_traces.lock();
		  tracefile << threadIdx << " " << coreIdx << " - "  << " I " << (wrongPathAddr + lineSize*i) << " 64" << std::endl;
          mtx_traces.unlock();
        } else {
		  tracefile << threadIdx << " " << coreIdx << " - "  << " I " << (wrongPathAddr + lineSize*i) << " 64" << std::endl;
        }
        //previous_instr = 0;
	    }
	}

        fetchCycle = lastCommitCycle;
    }
    branchPc = 0;  // clear for next BBL

    // Offload next Block
    if(only_offload){
	ptrace = print_trace;
	print_trace = false;
    }

    // Simulate current bbl ifetch
    Address endAddr = bblAddr + bblInfo->bytes;
    for (Address fetchAddr = bblAddr; fetchAddr < endAddr; fetchAddr += lineSize) {
        // The Nehalem frontend fetches instructions in 16-byte-wide accesses.
        // Do not model fetch throughput limit here, decoder-generated stalls already include it
        // We always call fetches with curCycle to avoid upsetting the weave
        // models (but we could move to a fetch-centric recorder to avoid this)
        uint64_t fetchLat = l1i->load(fetchAddr, curCycle, instrs) - curCycle;
        cRec.record(curCycle, curCycle, curCycle + fetchLat);
        fetchCycle += fetchLat;
        // LOIS
        if( pim_trace && instr_trace && ptrace){
            //THREAD_ID PROCESSOR_ID  CYCLE_NUM TYPE  ADDRESS SIZE
           if(merge_hostTraces){
             mtx_traces.lock();
             tracefile << threadIdx << " " << coreIdx << " - "  << " I " << fetchAddr << " 64" << std::endl;
             mtx_traces.unlock();
           } else {
             tracefile << threadIdx << " " << coreIdx << " - "  << " I " << fetchAddr << " 64" << std::endl;
           }
             //previous_instr = 0;
        }
    }

    // If fetch rules, take into account delay between fetch and decode;
    // If decode rules, different BBLs make the decoders skip a cycle
    decodeCycle++;
    uint64_t minFetchDecCycle = fetchCycle + (DECODE_STAGE - FETCH_STAGE);
    if (minFetchDecCycle > decodeCycle) {
#ifdef OOO_STALL_STATS
        profFetchStalls.inc(decodeCycle - minFetchDecCycle);
#endif
        decodeCycle = minFetchDecCycle;
    }
}

// Timing simulation code
void OOOCore::join() {
    DEBUG_MSG("[%s] Joining, curCycle %ld phaseEnd %ld", name.c_str(), curCycle, phaseEndCycle);
    uint64_t targetCycle = cRec.notifyJoin(curCycle);
    if (targetCycle > curCycle) advance(targetCycle);
    phaseEndCycle = zinfo->globPhaseCycles + zinfo->phaseLength;
    // assert(targetCycle <= phaseEndCycle);
    DEBUG_MSG("[%s] Joined, curCycle %ld phaseEnd %ld", name.c_str(), curCycle, phaseEndCycle);
}

void OOOCore::leave() {
    DEBUG_MSG("[%s] Leaving, curCycle %ld phaseEnd %ld", name.c_str(), curCycle, phaseEndCycle);
    cRec.notifyLeave(curCycle);
}

void OOOCore::cSimStart() {
    uint64_t targetCycle = cRec.cSimStart(curCycle);
    assert(targetCycle >= curCycle);
    if (targetCycle > curCycle) advance(targetCycle);
}

void OOOCore::cSimEnd() {
    uint64_t targetCycle = cRec.cSimEnd(curCycle);
    assert(targetCycle >= curCycle);
    if (targetCycle > curCycle) advance(targetCycle);
}

void OOOCore::advance(uint64_t targetCycle) {
    assert(targetCycle > curCycle);
    decodeCycle += targetCycle - curCycle;
    insWindow.longAdvance(curCycle, targetCycle);
    curCycleRFReads = 0;
    curCycleIssuedUops = 0;
    assert(targetCycle == curCycle);
    /* NOTE: Validation with weave mems shows that not advancing internal cycle
     * counters in e.g., the ROB does not change much; consider full-blown
     * rebases though if weave models fail to validate for some app.
     */
}

// Pin interface code

void OOOCore::LoadFunc(THREADID tid, ADDRINT addr, UINT32 size) {static_cast<OOOCore*>(cores[tid])->load(addr, size);}
void OOOCore::StoreFunc(THREADID tid, ADDRINT addr, UINT32 size) {static_cast<OOOCore*>(cores[tid])->store(addr, size);}

//LOIS
void OOOCore::OffloadBegin(THREADID tid) {static_cast<OOOCore*>(cores[tid])->offloadFunction_begin();}
void OOOCore::OffloadEnd(THREADID tid) {static_cast<OOOCore*>(cores[tid])->offloadFunction_end();}

void OOOCore::PredLoadFunc(THREADID tid, ADDRINT addr, BOOL pred, UINT32 size) {
    OOOCore* core = static_cast<OOOCore*>(cores[tid]);
    if (pred) core->load(addr, size);
    else core->predFalseMemOp();
}

void OOOCore::PredStoreFunc(THREADID tid, ADDRINT addr, BOOL pred, UINT32 size) {
    OOOCore* core = static_cast<OOOCore*>(cores[tid]);
    if (pred) core->store(addr, size);
    else core->predFalseMemOp();
}

void OOOCore::BblFunc(THREADID tid, ADDRINT bblAddr, BblInfo* bblInfo) {
  OOOCore* core = static_cast<OOOCore*>(cores[tid]);

  //if(core->get_offload_code()>0){
  //   core->bbl_offload();
  //} 


  core->bbl(bblAddr, bblInfo);
  
  while (core->curCycle > core->phaseEndCycle) {
    core->phaseEndCycle += zinfo->phaseLength;
     
    
    uint32_t cid = getCid(tid);
    core->coreIdx = cid;
    core->threadIdx = tid;
    // NOTE: TakeBarrier may take ownership of the core, and so it will be used by some other thread. If TakeBarrier context-switches us,
    // the *only* safe option is to return inmmediately after we detect this, or we can race and corrupt core state. However, the information
    // here is insufficient to do that, so we could wind up double-counting phases.
    uint32_t newCid = TakeBarrier(tid, cid);
    // NOTE: Upon further observation, we cannot race if newCid == cid, so this code should be enough.
    // It may happen that we had an intervening context-switch and we are now back to the same core.
    // This is fine, since the loop looks at core values directly and there are no locals involved,
    // so we should just advance as needed and move on.
    if (newCid != cid) break;  /*context-switch, we do not own this context anymore*/
  }

}

void OOOCore::BranchFunc(THREADID tid, ADDRINT pc, BOOL taken, ADDRINT takenNpc, ADDRINT notTakenNpc) {
    static_cast<OOOCore*>(cores[tid])->branch(pc, taken, takenNpc, notTakenNpc);
}

