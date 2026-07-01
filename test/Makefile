
.PHONY: all latest legacy effectiveTestor speedTestor throughput fastcdcParity jcParity fastcdcGpuPtx ncuChunkingProfile summarizeChunkingResults sizeDist rapidCDCSizeDist perfromanceBreakdown chunkingTool chunkingSuite

all: latest

latest: chunkingTool chunkingSuite fastcdcParity jcParity

legacy: effectiveTestor speedTestor throughput sizeDist rapidCDCSizeDist perfromanceBreakdown

effectiveTestor:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o effectiveTestor effectiveTestor.c JC.c rabin.c ae.c leap.c Rabin_simple.c fastcdc.c -lm -lcrypto -lssl

speedTestor:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o speedTestor speedTestor.c read.c JC.c rabin.c ae.c leap.c sds.c fastcdc.c Rabin_simple.c -lm -lcrypto -lssl -pthread

throughput:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o throughput throughput.c read.c JC.c rabin.c ae.c leap.c sds.c fastcdc.c Rabin_simple.c -lm -lcrypto -lssl -pthread

fastcdcParity:
	gcc -ggdb -Wall -O3 $(shell pkg-config --cflags glib-2.0) -o fastcdcParity fastcdc_parity.c ../src/chunking/libchunk.a -lm -lcrypto -lssl $(shell pkg-config --libs glib-2.0) -ldl -pthread

jcParity:
	gcc -ggdb -Wall -O3 $(shell pkg-config --cflags glib-2.0) -o jcParity jc_parity.c ../src/chunking/libchunk.a -lm -lcrypto -lssl $(shell pkg-config --libs glib-2.0) -ldl -pthread

fastcdcGpuPtx:
	$(MAKE) -C ../src/chunking fastcdc_gpu_ptx

ncuChunkingProfile:
	sh ./run_ncu_chunking_profile.sh ../README.md ./ncu-results

summarizeChunkingResults:
	python3 ./summarize_chunking_results.py ./ncu-results/ncu_summary.csv ./ncu-results/merged_results.csv

sizeDist:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o sizeDist sizeDist.c -lm

rapidCDCSizeDist:
	gcc -o rapidCDCSizeDist rapidCDCSizeDist.c -lm

perfromanceBreakdown:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o perfromanceBreakdown perfromanceBreakdown.c read.c JC.c rabin.c ae.c leap.c sds.c fastcdc.c Rabin_simple.c -lm -lcrypto -lssl -pthread -lglib -lxxhash

chunkingTool: ../src/chunking/libchunk.a chunkingTool.c chunk_tool_timing.c
	gcc -ggdb -Wall -O3 $(shell pkg-config --cflags glib-2.0) -o chunkingTool chunkingTool.c chunk_tool_timing.c ../src/chunking/libchunk.a -lm -lcrypto -lssl $(shell pkg-config --libs glib-2.0) -ldl -pthread

chunkingSuite: chunkingTool run_chunking_suite.sh
	sh ./run_chunking_suite.sh ../README.md

../src/chunking/libchunk.a:
	$(MAKE) -C ../src/chunking libchunk.a
	
