#!/bin/bash
ZSIMPATH=$(pwd)
PINPATH="$ZSIMPATH/pin"
LIBCONFIGPATH="$ZSIMPATH/libconfig"
RAMULATORPATH="$ZSIMPATH/ramulator"

#export LIBCONFIGPATH
#cd $LIBCONFIGPATH
#make clean-libtool
#cd ..

export RAMULATORPATH
cd $RAMULATORPATH
make clean
cd ..

export PINPATH
scons -c
