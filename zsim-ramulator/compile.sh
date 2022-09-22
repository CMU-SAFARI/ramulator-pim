#!/bin/bash
ZSIMPATH=$(pwd)
PINPATH="$ZSIMPATH/pin"
LIBCONFIGPATH="${CONDA_PREFIX}"
NUMCPUS=$(grep -c ^processor /proc/cpuinfo)

echo "Compiling only ZSim ..."
export PINPATH
export LIBCONFIGPATH
scons -j$NUMCPUS
