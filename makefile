all: ftserver

ftserver: ftserver.o
	gcc -std=gnu99 -o ftserver ftserver.o

ftserver.o: ftserver.c
	gcc -std=gnu99 -c ftserver.c

clean:
	rm ftserver.o
	rm ftserver
