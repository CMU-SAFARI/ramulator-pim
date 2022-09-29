#!/bin/bash

cd common/DRAMPower
make clean
cd -

cd ramulator
make clean
cd -

cd zsim-ramulator
export PINPATH="${PWD}/pin"
scons -c
rm -rf build .sconsign.dblite
cd -
