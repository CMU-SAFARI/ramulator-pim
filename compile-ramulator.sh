cd ./common/DRAMPower
XERCES_ROOT=${CONDA_PREFIX} CXX=${CONDA_PREFIX}/bin/x86_64-conda_cos6-linux-gnu-c++ make -j
cd ../../ramulator
CXX=${CONDA_PREFIX}/bin/x86_64-conda_cos6-linux-gnu-c++ make -j
