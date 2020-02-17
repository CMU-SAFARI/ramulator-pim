#!/bin/bash
apt-get -y install build-essential
apt-get -y install scons
apt-get -y install automake
apt-get -y install autoconf
apt-get -y install m4
apt-get -y install perl
apt-get -y install flex
apt-get -y install bison
apt-get -y install byacc
#apt-get -y install libconfig-dev
#apt-get -y install libconfig++-dev
apt-get -y install libhdf5-dev
apt-get -y install libelf-dev
apt-get -y install libxerces-c-dev

ln -s /usr/include/asm-generic /usr/include/asm
