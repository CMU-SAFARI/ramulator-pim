#!/bin/bash
ZSIMPATH=$(pwd)
PINPATH="$ZSIMPATH/pin"
LIBCONFIGPATH="$ZSIMPATH/libconfig"
RAMULATORPATH="$ZSIMPATH/ramulator"

#export LIBCONFIGPATH
#cd $LIBCONFIGPATH
#make clean-libtool
#cd ..

export PINPATH
scons -c
