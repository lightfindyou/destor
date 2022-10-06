#!/bin/bash 
#avgChunk=(8192 16384)
#avgChunk=(4096 8192 16384 32768 65536)
#avgChunk=(65536 32768 16384 8192 4096)
#avgChunk=(65536)
avgChunk=(8192)
maxChunk=(16384)
# maxChunk=(8192 16384 32768 65536 131072)
#avgChunk=(4096)
#maxChunk=(8192)
#DedupDIR=("/home/xzjin/backupData/bbcNews/" "/home/xzjin/backupData/gcc/")
#DedupDIR=("/home/xzjin/backupData/bbcNews/" "/home/xzjin/backupData/Paper/")
# DedupDIR=("/home/xzjin/backupData/bbcNews/")
# DedupDIR=("/home/xzjin/backupData/Paper/")
# DedupDIR=("/home/xzjin/backupData/VMI")
# DedupDIR=("/home/xzjin/backupData/bbcNews/" "/home/xzjin/backupData/gcc/" "/home/xzjin/backupData/Paper/" "/home/xzjin/backupData/VMB/" "/home/xzjin/backupData/VMI/")
# DedupDIR=("/home/xzjin/backupData/Paper/")
DedupDIR=("/home/xzjin/backupData/gcc/")
# DedupDIR=("/home/xzjin/backupData/gcc_part/")
#parIdx={ 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 }
parIdx=(0)
mto=("1" "2" "3" "4" "5" "6" "7" "8")
#chunkMethod=( "gear" "rabin" "rabin_simple" "rabinJump" "nrRabin" "TTTD" "AE" "fastCDC" "leap" "JC" "algNum" "normalized-gearjump")

# chunkMethod=("rabin" "TTTD" "AE" "fastCDC" "JC" "leap" )
# chunkMethod=( "AE" "JC" "leap" )
# chunkMethod=("rabin" "rabinJump")
# chunkMethod=("gear" "JC" "rabin" "rabinJump")
# chunkMethod=("normalized-gearjump" "JC")
chunkMethod=("JC")
# chunkMethod=("gear" "JC" "TTTDGear")
# chunkMethod=("gear" "TTTDGear" "JCTTTD")
# chunkMethod=("fastCDC")
#chunkMethod=("TTTDGear")
# chunkMethod=("JCTTTD")
# chunkMethod=("gear" "TTTDGear" "JC")
# chunkMethod=("gear")
# chunkMethod=("TTTD")
# chunkMethod=("rabin" "TTTD" "AE" "fastCDC" "JC" "leap" )


for m in "${mto[@]}"
do
	for p in "${parIdx[@]}"
	do
		for s in "${!avgChunk[@]}"
		do
			for dir in "${DedupDIR[@]}"
			do
				for a in "${chunkMethod[@]}"
				do
					set -o xtrace
					time ./speedTestor -d $dir -c ${avgChunk[s]} -p $p -a $a -m $m
					set +o xtrace
					echo; echo;
				done
			done
		done
	done
done
