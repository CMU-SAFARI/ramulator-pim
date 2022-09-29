#!/bin/bash

cd zsim-ramulator

export PINPATH="${PWD}/pin"
export LIBCONFIGPATH="${CONDA_PREFIX}"

NUMCPUS=$(grep -c ^processor /proc/cpuinfo)
scons -j$NUMCPUS

cd -
