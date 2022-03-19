all:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o testor effectiveTestor.c JC.c rabin_chunking.c ae_chunking.c leap.c -lm -lcrypto -lssl

	gcc -ggdb -Wall -I/usr/local/include -O3 -o speedTestor speedTestor.c read.c JC.c rabin_chunking.c ae_chunking.c leap.c sds.c -lm -lcrypto -lssl -pthread