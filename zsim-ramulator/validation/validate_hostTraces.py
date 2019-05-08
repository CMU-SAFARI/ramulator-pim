#!/usr/bin/python

import h5py
import numpy as np
import sys
import os
from subprocess import call
import commands
import re

# Print TRACE IPC stats
def traceIPC(fname):
    inputFile = open(fname, "r")

    delta = 0
    intial = 0
    final = 0
    intr = 0
    totalInstructions = 0
    misses = 0
    #print fname

    for line in inputFile:
        match = re.search(r'\d\s\d\s\d+\s\w\s', line)
        if(match):
            try:
                instr = int (match.group().split()[2])
                type = match.group().split()[3]
		totalInstructions+=instr
                if(type != "P"):
		   totalInstructions+=1
                misses += 1
            except:
                print "Error at line - "+line
    print "[overall-ROI] instrs: "+str(totalInstructions)+" misses: "+str(misses)+" MPKI: "+str(float(misses*1000)/float(totalInstructions))

# Print ZSIM IPC stats
def zsimIPC(fname):
    # Read the ZSIM stats file, in hdf5 format
    fzsim = h5py.File(fname,"r")

    # Get the single dataset in the file
    dset = fzsim["stats"]["root"]
    #print "dset_size: "+str(dset.size)

    findex = dset.size - 1 # We just need to access to the final stats (dont need to know intermeddiate stats)
    num_cores = dset[findex]['core'].size # get the number of cores
    print "cores: "+str(num_cores)
    cycles = [0] * num_cores
    instructions = [0] * num_cores
    ipc = [0] * num_cores
    for c in range(0, num_cores):
        cycles[c] = dset[findex]['core'][c]['cycles']
        instructions[c] = dset[findex]['core'][c]['instrs']
        if cycles[c] != 0:
          ipc[c] = float(instructions[c])/float(cycles[c])
          print "[core-"+str(c)+"] cycles: "+str(cycles[c])+" instrs: "+str(instructions[c])+" IPC: "+str(ipc[c])
    print "[overall] cycles: "+str(np.sum(cycles))+" instrs: "+str(np.sum(instructions))+" IPC: "+str(float(np.sum(instructions))/float(np.sum(cycles)))
    misses = np.sum(dset[findex]['l3']["mGETS"])
    misses += np.sum(dset[findex]['l3']["mGETXIM"])
    misses += np.sum(dset[findex]['l3']["mGETXSM"])
    print "MPKI: "+str(float(misses)*1000/float(np.sum(instructions)))

#####################################################
# MAIN
#####################################################
# The imput is two files: zsim stats, and the trace
if len(sys.argv) != 3:
    print "./IPC_validation.py zsim.h5 trace.out"
    sys.exit();

print "## ZSIM Stats (from the whole ROI)##"
zsimIPC(sys.argv[1])
print "##TRACE Stats##"
traceIPC(sys.argv[2])



