# C++ compiler and flags
CC = g++
CFLAGS = -g -fopenmp -O2

all: copy copy_offload

copy: copy.cpp
	$(CC) $(CFLAGS) $< -o $@

copy_offload: copy.cpp
	$(CC) $(CFLAGS) -DOMP_OFFLOAD $< -o $@

clean:
	rm -f copy
