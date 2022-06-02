#!/bin/sh

# avgChunk=(8192 16384 32768 65536)
avgChunk=(4096 8192 16384 32768 65536)
# maxChunk=(8192 16384 32768 65536 131072)
#avgChunk=(4096)
# maxChunk=(8192)
# DedupDIR=("/home/xzjin/backupData/bbcNews/" "/home/xzjin/backupData/gcc")
# DedupDIR=("/home/xzjin/backupData/bbcNews/")
# DedupDIR=("/home/xzjin/backupData/Paper")
# DedupDIR=("/home/xzjin/backupData/VMI")
# DedupDIR=("/home/xzjin/backupData/bbcNews/" "/home/xzjin/backupData/gcc/" "/home/xzjin/backupData/Paper/" "/home/xzjin/backupData/VMB/" "/home/xzjin/backupData/VMI/")
# DedupDIR=("/home/xzjin/backupData/Paper/")
#DedupDIR=("/home/xzjin/backupData/gcc/")
DedupDIR=("/home/xzjin/backupData/gcc_part1/")
#parIdx={ 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 }
parIdx=(0)

for p in "${parIdx[@]}"
do
	for s in "${!avgChunk[@]}"
	do
		for dir in "${DedupDIR[@]}"
		do
			set -o xtrace
			./speedTestor -d $dir -c ${avgChunk[s]} -p $p
			set +o xtrace
			echo; echo;
		done
	done
done
