bshell: bshell.h main.c bshell.c builtin.c process.c
		gcc -Wall -o bshell main.c bshell.c builtin.c process.c
clean:
		rm -f bshell a.out
