#!/bin/bash

# https://github.com/rapidsai/cuml/issues/3620
conda install --yes gcc_linux-64=7.2.0 gxx_linux-64=7.2.0
conda install --yes -c conda-forge sysroot_linux-64=2.17

# For Ramulator
conda install --yes -c conda-forge xerces-c
conda install --yes -c conda-forge boost

# For ZSim
conda install --yes -c anaconda scons
conda install --yes -c conda-forge automake
conda install --yes -c conda-forge autoconf
conda install --yes -c conda-forge m4
conda install --yes -c conda-forge perl
conda install --yes -c conda-forge flex
conda install --yes -c conda-forge bison
conda install --yes -c conda-forge hdf5
conda install --yes -c conda-forge elfutils
conda install --yes -c conda-forge libconfig

# apt-get -y install build-essential
# apt-get -y install byacc
# ln -s /usr/include/asm-generic /usr/include/asm
