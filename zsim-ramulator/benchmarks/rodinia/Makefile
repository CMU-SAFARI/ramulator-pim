# C++ compiler
CC = g++
CFLAGS = -g -fopenmp -O2 -Wno-unused-result

all: bfs bfs_offload

bfs: bfs.cpp
	$(CC) $(CFLAGS) bfs.cpp -o bfs

bfs_offload: bfs.cpp
	$(CC) $(CFLAGS) -DOMP_OFFLOAD bfs.cpp -o bfs_offload

clean:
	rm -f bfs bfs_offload result.txt
