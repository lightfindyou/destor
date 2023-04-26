all:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o effectiveTestor effectiveTestor.c JC.c rabin.c ae.c leap.c Rabin_simple.c fastcdc.c -lm -lcrypto -lssl

	gcc -ggdb -Wall -I/usr/local/include -O3 -o speedTestor speedTestor.c read.c JC.c rabin.c ae.c leap.c sds.c fastcdc.c Rabin_simple.c -lm -lcrypto -lssl -pthread

	gcc -ggdb -Wall -I/usr/local/include -O3 -o throughput throughput.c read.c JC.c rabin.c ae.c leap.c sds.c fastcdc.c Rabin_simple.c -lm -lcrypto -lssl -pthread

	gcc -ggdb -Wall -I/usr/local/include -O3 -o sizeDist sizeDist.c -lm 
	gcc -o rapidCDCSizeDist rapidCDCSizeDist.c -lm 
