#! /bin/sh

LD_PRELOAD=/home/xzjin/Documents/destor/src/.libs/libentrance.so.0.0.0  /home/xzjin/leveldb/build/db_bench --benchmarks=fillrandom --db=/pmem/db128
echo;
echo;

/home/xzjin/leveldb_modified/build/db_bench --benchmarks=fillrandom --db=/pmem/db128
