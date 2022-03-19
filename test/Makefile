all:
	gcc -ggdb -Wall -I/usr/local/include -O3 -o testor effectiveTestor.c JC.c rabin.c ae.c leap.c -lm -lcrypto -lssl

	gcc -ggdb -Wall -I/usr/local/include -O3 -o speedTestor speedTestor.c read.c JC.c rabin.c ae.c leap.c sds.c fastcdc.c -lm -lcrypto -lssl -pthread