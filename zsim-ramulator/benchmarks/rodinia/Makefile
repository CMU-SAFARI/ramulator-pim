# C compiler
CC = g++
ICC = gcc #icc
CC_FLAGS = -g -fopenmp -O2

all: bfs bfs_offload

bfs: bfs.cpp 
	$(CC) $(CC_FLAGS) bfs.cpp -o bfs 

bfs_offload: bfs.cpp
	$(ICC) $(CC_FLAGS) -DOMP_OFFLOAD bfs.cpp -o bfs_offload

clean:
	rm -f bfs bfs_offload result.txt
