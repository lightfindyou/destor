all:
	gcc -o target target.c
	gcc -fPIC -shared  -o replaceFun.so replaceFun.c -ldl
clean:
	/usr/bin/rm target replaceFun.so
