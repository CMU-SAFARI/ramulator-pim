#!/bin/bash
ZSIMPATH=$(pwd)
PINPATH="$ZSIMPATH/pin"
LIBCONFIGPATH="$ZSIMPATH/libconfig"
NUMCPUS=$(grep -c ^processor /proc/cpuinfo)



if [ "$1" = "z" ]
then
	echo "Compiling only ZSim ..."
        export PINPATH
        export LIBCONFIGPATH
        scons -j$NUMCPUS
else
	echo "Compiling all ..."
	export LIBCONFIGPATH
	cd $LIBCONFIGPATH
	./configure --prefix=$LIBCONFIGPATH && make install
	cd ..

	export PINPATH
	scons -j$NUMCPUS
fi
