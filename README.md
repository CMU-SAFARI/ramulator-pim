
# ZSim+Ramulator - A Processing-in-Memory Simulation Framework

ZSim+Ramulator is a framework for design space exploration of general-purpose Processing-in-Memory (PIM) architectures. 
The framework is based on two widely-known simulators: ZSim [[1]](https://github.com/s5z/zsim/) and Ramulator [[2]](https://people.inf.ethz.ch/omutlu/pub/ramulator_dram_simulator-ieee-cal15.pdf)[[3]](https://github.com/CMU-SAFARI/ramulator). 

We consider a computing system that includes host CPU cores and general-purpose PIM cores. The PIM cores are placed in the logic layer of a 3D-stacked memory (Ramulator's HMC model).
With this simulation framework, we can simulate host CPU cores and general-purpose PIM cores with the aim of comparing the performance of both for an application or parts of it.
The simulation framework does not currently support concurrent execution on host and PIM cores.

We use ZSim to generate memory traces that are fed to Ramulator. We modified ZSim to generate two type of traces: 1) filtered traces for simulation of the host CPU cores, and 2) unfiltered traces for the simulation of the PIM cores.

1) Filtered traces: We obtain them by gathering memory requests at the memory controller. This way, the cache hierarchy of the host (including the coherence protocol) is simulated in ZSim. ZSim can also simulate hardware prefetchers.

2) Unfiltered traces: We obtain them by gathering all memory requests as soon as they are issued by the core pipeline.

Ramulator simulates the memory accesses of the host cores and the PIM cores by using, respectively, the filtered and unfiltered traces produced with ZSim. 
Ramulator contains simple models of out-of-order and in-order cores that can be used for simulation of host and PIM. 
For the simulation of the PIM cores in the logic layer of 3D-stacked memory, we modified Ramulator to avoid the overheads of the off-chip link.

## Citation

Please cite the following papers if you find this simulation infrastructure useful:

Yoongu Kim, Weikun Yang, and Onur Mutlu, "[Ramulator: A Fast and Extensible DRAM Simulator](https://people.inf.ethz.ch/omutlu/pub/ramulator_dram_simulator-ieee-cal15.pdf)". IEEE Computer Architecture Letters (CAL), March 2015. 

Gagandeep Singh, Juan Gomez-Luna, Giovanni Mariani, Geraldo Francisco de Oliveira, Stefano Corda, Sander Stujik, Onur Mutlu, and Henk Corporaal, "[NAPEL: Near-Memory Computing Application Performance Prediction via Ensemble Learning](https://people.inf.ethz.ch/omutlu/pub/NAPEL-near-memory-computing-performance-prediction-via-ML_dac19.pdf)". Proceedings of the 56th Design Automation Conference (DAC), Las Vegas, NV, USA, June 2019.

## Repository Structure and Installation

We point out next the repository structure and some important folders and files.
```
.
+-- README.md
+-- common/
|   +-- DRAMPower/
+-- ramulator/
|   +-- Configs/
|   +-- sample_traces/
|	|	+-- host/
|	|	+-- pim/
|	+-- src/
+-- zsim-ramulator/
|   +-- benchmarks/
|	|	+-- rodinia/
|   +-- clean.sh
|   +-- compile.sh
|   +-- setup.sh
|   +-- tests/
|	|	+-- host.cfg
|	|	+-- host_prefetch.cfg
|	|	+-- pim.cfg
```

### Prerequisites

Our framework requires both ZSim and Ramulator dependencies. 
* Ramulator requires a C++11 compiler (e.g., clang++, g++-5).
* ZSim requires gcc >=4.6, pin, scons, libconfig, libhdf5, libelfg0.
We provide two scripts `setup.sh` and `compile.sh` under `zsim-ramulator` to facilitate ZSim's installation. The first one installs all ZSim's dependencies. The second one compiles ZSim. 

### Installing

To install ZSim:
```
cd zsim-ramulator
sudo sh setup.sh
sh compile.sh
```

To install Ramulator:
```
cd ramulator
make -j
```

Alternatively, to resolve all dependencies on the common libraries, you can use the below commands:
```
sh compileramulator.sh
```


## Generating Traces with ZSim 

There are three steps to generate traces with ZSim:
1. Instrument the code with the hooks provided in `zsim-ramulator/misc/hooks/zsim_hooks.h`.
2. Create configuration files for ZSim.
3. Run.

Next, we describe the three steps in detail:

1. First, we identify the application's hotspot. We refer to it as `offload` region, i.e., the region of code that will run in the PIM cores. We instrument the application by including the following code: 

```cpp
#include "zsim-ramulator/misc/hooks/zsim_hooks.h"
foo(){
    /*
    * zsim_roi_begin() marks the beginning of the region of interest (ROI).
    * It must be included in a serial part of the code.
    */
	zsim_roi_begin(); 
	zsim_PIM_function_begin(); // Indicates the beginning of the code to simulate (hotspot).
	...
	zsim_PIM_function_end(); // Indicates the end of the code to simulate.
    /*
    * zsim_roi_end() marks the end of the ROI. 
    * It must be included in a serial part of the code. 
    */
	zsim_roi_end(); 
}
```

2. Second, we create the configuration files to execute the application using ZSim. Sample configuration files for filtered (`host.cfg`, `host_prefetch.cfg`) and unfiltered (`pim.cfg`) traces are provided under `zsim-ramulator/tests/`. Please, check those files to understand how to configure the number of cores, number of caches and their sizes, and number prefetchers. Next, we describe other important knobs that can be changed in the configuration files:


* `only_offload=true|false`: When set to `true`, ZSim will generate traces only between `zsim_PIM_function_begin` and `zsim_PIM_function_end`. When set to `false`, it will generate traces for the whole ROI. 
* `pim_traces=true|false`: When set to `true`, ZSim will generate unfiltered traces. When set to `false`, it will generate filtered traces.
* `instr_traces=true|false`: When set to `true`, ZSim will also get traces for Instruction Cache Misses.
* `outFile=string`: Name of the output file.
* `max_offload_instrs`: Maximum number of offload instructions to instrument. 
* `merge_hostTraces = true|false`: When set to `true`, ZSim will generate a single file with the whole trace for N Cores. When set to `false`, it will generate one trace file per core. Setting this flag to `true` slows down the trace collection significantly due to syncronization. 

3. Third, we run ZSim:
```
./build/opt/zsim configuration_file.cfg
```

### Trace format

ZSim generates traces with the following format:
```
THREAD_ID PROCESSOR_ID INSTR_NUM TYPE ADDRESS SIZE
```
where `TYPE` can be:
* `L`: Memory trace of a load (read) request
* `S`: Memory trace of a store (write) request
* `P`: Memory trace of a prefetching request
* `I`: Memory trace of an instruction request

The other fields in the trace file are:
* `THREAD_ID`: Which thread generated the request.
* `PROCESSOR_ID`: Which core generated the request. 
* `INSTR_NUM`: The number of non-memory instructions that were executed before the memory request was generated.
* `ADDRESS`: The virtual memory address of the memory request.
* `SIZE`: The size of the memory request in bytes.

## Example Trace Generation

Under `zsim_ramulator/benchmarks` we provide a sample instrumented code for the BFS implementation included in the Rodinia benchmarks suite. The code is under `zsim_ramulator/benchmarks/rodinia`. We instrument the function `BFSGraph`. To compile the application:

```
cd zsim_ramulator/benchmarks/rodinia/
make 
```

The sample ZSim configuration files under `tests` (`host.cfg`, `host_prefetch.cfg`,`pim.cfg`) are configured to read the binary generated by the previous step and run the BFS application. Therefore, to generate the trace files, from `zsim-ramulator`:

```
./build/opt/zsim tests/host.cfg
./build/opt/zsim tests/host_prefetch.cfg
./build/opt/zsim tests/pim.cfg
```
These will generate the trace files under `zsim-ramulator` (`rodiniaBFS.out.*`,`rodiniaBFS-prefetcher.out.*`, `pim-rodiniaBFS.out.*`, respectively).


## Running Ramulator

This is a modified version of Ramulator for simulation of PIM architectures. This version of Ramulator can simulate both host and PIM cores. 
To configure the mode, include in the configuration file (.cfg) one of the next two options:
* pim_mode = 0 (for host)
* pim_mode = 1 (for pim)

Sample configuration files are provided under `ramulator/Configs/`.

Some other knobs that can be configured are:
* `--config`: Ramulator's configuration file.
* `--stats`: The file to which Ramulator will write the results of the simulator. 
* `--trace`: The trace file that should be loaded.
* `--disable-per-scheduling true|false`: When set to `true`, it enables perfect memory scheduling, where each memory request is placed in the respective vault based on HMC interleaving. When set to `false`, the requests are scheduled based on the `PROCESSOR_ID` in each memory trace of the trace file. 
* `--core-org=outOrder|inOrder`: For simulation of out-of-order or in-order cores.
* `--number-cores=`: Number of cores to simulate.
* `--trace-format=zsim|pin|pisa`: Defines the source of the memory traces. Use `zsim` for filtered or unfiltered traces generated with our modified ZSim. Other options are traces generated with a [Pin tool](https://software.intel.com/en-us/articles/pin-a-dynamic-binary-instrumentation-tool) or with [PISA](https://github.com/exabounds/ibm-pisa).
* `--split-trace=true|false`: When set to `true`, Ramulator will open a single trace file, store it in memory, and split the trace file among the `number-cores` cores according to the core-id in the trace. When set to `false`, Ramulator will open one trace file per core and read it line-by-line during the simulation (it expects each trace to end with .core_id -- from 0 ... `number-cores`-1). We strongly suggest to use `--split-trace=true` because large trace files might lead to memory overload.
* `--mode=cpu`: Ramulator can simulate either a system with cpu and DRAM, or only the DRAM by itself. Ramulator must operate in CPU trace mode for this framework.

Sample ZSim trace files are provided under `sample_traces/`. Before using them, decompress each trace file. 

To run the `host` simulation:
```
./ramulator --config Configs/host.cfg --disable-perf-scheduling true --mode=cpu --stats host.stats --trace sample_traces/host/rodiniaBFS.out --core-org=outOrder --number-cores=4 --trace-format=zsim --split-trace=true
```
To run the `PIM` simulation:
```
./ramulator --config Configs/pim.cfg --disable-perf-scheduling true --mode=cpu --stats pim.stats --trace sample_traces/pim/pim-rodiniaBFS.out --core-org=outOrder --number-cores=4 --trace-format=zsim --split-trace=true
```


## Acknowledgments 

The development of this simulation framework was partially supported by the Semiconductor Research Corporation. We also thank our industrial partners, especially Alibaba, Facebook, Google, Huawei, Intel, Microsoft, and VMware, for their generous donations. 
