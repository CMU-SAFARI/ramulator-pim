#ifndef __PROCESSOR_H
#define __PROCESSOR_H

#include "Cache.h"
#include "Config.h"
#include "Memory.h"
#include "Request.h"
#include "HMC.h"
#include "Controller.h"
#include "HMC_Memory.h"
#include "Statistics.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <ctype.h>
#include <functional>
#include <queue>
#include <deque>
namespace ramulator 
{

struct SqueduleQueue{
    deque<long> bubble_cnt;
    deque<long> req_addr;
    deque<bool> memory_rqst_sent;
    deque<unsigned> size;
    deque<Request::Type> req_type;

    int numberInstructionsInQueue = 0;
    bool is_empty(){
        if (numberInstructionsInQueue == 0 )
            return true;
        return false;
    }
    void pop_front(){
        if(!is_empty()){
   //         if(bubble_cnt.front()==0 && memory_rqst_sent.front()){
                bubble_cnt.pop_front();
                req_addr.pop_front();
             //   memory_rqst_sent.pop_front();
                req_type.pop_front();
                numberInstructionsInQueue--;
            //}
        }
    }
};

struct line{
    unsigned threadID;
    unsigned processorID;
    unsigned cycleNum;
    string type;
    unsigned long addr;
    unsigned size;
};

class Trace {
public:
    Trace() {}
    Trace(const string& trace_fname);
    bool init_trace(const string& trace_fname);
    ~Trace(){
        if(file.is_open()) file.close();
    }
    // trace file format 1:
    // [# of bubbles(non-mem instructions)] [read address(dec or hex)] <optional: write address(evicted cacheline)>
    bool get_unfiltered_request(long& bubble_cnt, long& req_addr, Request::Type& req_type);
    bool get_filtered_request(long& bubble_cnt, long& req_addr, Request::Type& req_type);
    bool get_pisa_request(long& bubble_cnt, long& req_addr, Request::Type& req_type, unsigned& size);
    bool get_zsim_request(long& bubble_cnt, long& req_addr, Request::Type& req_type, unsigned& cpu_id);

    // trace file format 2:
    // [address(hex)] [R/W]
    bool get_dramtrace_request(long& req_addr, Request::Type& req_type);
    long expected_limit_insts = 0;
    bool pim_mode_enabled = false;

private:
    std::ifstream file;
    std::string trace_name;
    std::vector<int> instructions;

    std::vector<std::string> split_str(std::string to_split);
};


class Window {
public:
    int ipc = 4;
    int depth = 128;

    Window() : ready_list(depth), addr_list(depth, -1) {}
    bool is_full();
    bool is_empty();
    void insert(bool ready, long addr);
    long retire();
    void set_ready(long addr, int mask);


    int load = 0;
    int head = 0;
    int tail = 0;
    std::vector<bool> ready_list;
    std::vector<long> addr_list;
};


class Core {
public:
    long clk = 0;
    long retired = 0;
    int id = 0;
    bool split_trace = false;
    int position;
    int num_requests = 0;
    unsigned inFlightMemoryAccess = 0;
    string cpu_type;
    bool pim_mode_enabled = false;
    function<bool(Request)> send;
    SqueduleQueue rqst_queue;
    bool lock_core = false;
    long prevnumberInstructionsInQueue = 0;
    Core(const Config& configs, int coreid,
        function<bool(Request)> send_next, Cache* llc,
        std::shared_ptr<CacheSystem> cachesys, MemoryBase& memory);

    void tick();
    void receive(Request& req);
    double calc_ipc();
    bool finished();
    bool has_reached_limit();
    void tick_inOrder();
    void tick_outOrder();
    void get_first_request();
    void load_trace(string trace_base_name);

    function<void(Request&)> callback;

    bool no_core_caches = true;
    bool no_shared_cache = true;
    int l1_size = 1 << 15;
    int l1_assoc = 1 << 3;
    int l1_blocksz = 1 << 6;
    int l1_mshr_num = 16;

    int l2_size = 1 << 18;
    int l2_assoc = 1 << 3;
    int l2_blocksz = 1 << 6;
    int l2_mshr_num = 16;

    std::vector<std::shared_ptr<Cache>> caches;
    Cache* llc;

    ScalarStat record_cycs;
    ScalarStat record_insts;

    long expected_limit_insts;
    // This is set true iff expected number of instructions has been executed or all instructions are executed.
    bool reached_limit = false;

    Window window;

    long bubble_cnt;
    long req_addr = -1;
    Request::Type req_type;
    bool more_reqs;
    long last = 0;
    
    ScalarStat memory_access_cycles;
    ScalarStat cpu_inst;
    ScalarStat idle_cycles;
    ScalarStat memory_inst;
    ScalarStat non_memory_inst;
    MemoryBase& memory;
    Trace trace_per_core;

};

class Processor {
public:
    Processor(const Config& configs, vector<string> trace_list,
        function<bool(Request)> send, MemoryBase& memory);
    void tick();
    void receive(Request& req);
    bool finished();
    void calc_stats();
    bool has_reached_limit();
    bool pim_mode_enabled = false;
    bool pisa_trace = false;
    bool zsim_trace = false;
    std::vector<std::unique_ptr<Core>> cores;
    std::vector<double> ipcs;
    double ipc = 0;
    long total_retired = 0;
    long total_instructions = 0;
    double cycle_time;
    // When early_exit is true, the simulation exits when the earliest trace finishes.
    bool early_exit;

    bool no_core_caches = true;
    bool no_shared_cache = true;

    int l3_size = 1 << 23;
    int l3_assoc = 1 << 3;
    int l3_blocksz = 1 << 6;
    int mshr_per_bank = 16;

    int number_cores;

    std::shared_ptr<CacheSystem> cachesys;
    Cache llc;

    ScalarStat cpu_cycles;
    ScalarStat total_idle_cycles;
    ScalarStat average_idle_cycles;
    ScalarStat total_cache_misses;
    ScalarStat total_cache_hits;
    ScalarStat general_ipc;
    ScalarStat total_cpu_instructions;
    ScalarStat total_time;
    Trace trace;
    MemoryBase& memory;
};

}
#endif /* __PROCESSOR_H */
