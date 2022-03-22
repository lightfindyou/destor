#!/bin/sh

#avgChunk=(4096 8192 16384 32768 65536)
#maxChunk=(8192 16384 32768 65536 131072)
avgChunk=(4096)
maxChunk=(8192)
#DedupDIR=("/home/xzjin/backupData/bbcNews/" "/home/xzjin/backupData/gcc")
#DedupDIR=("/home/xzjin/backupData/bbcNews/")
#DedupDIR=("/home/xzjin/backupData/Paper")
#DedupDIR=("/home/xzjin/backupData/VMI")
#DedupDIR=("/home/xzjin/backupData/bbcNews/" "/home/xzjin/backupData/gcc/" "/home/xzjin/backupData/Paper/" "/home/xzjin/backupData/VMB/" "/home/xzjin/backupData/VMI/")
DedupDIR=("/home/xzjin/backupData/Paper/")


for s in "${!avgChunk[@]}"
do
	for dir in "${DedupDIR[@]}"
	do
		echo "Dedup dir: "$dir"; chunk size: "${avgChunk[s]}
		/home/xzjin/src/destor/test/speedTestor -d $dir
		echo; echo;
	done
done
