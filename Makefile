CFLAGS=-O3 -g -Wall -Impt

all: mptevents
mptevents: mptevents.o | Makefile
mptevents.o: mptevents.c | Makefile
tags: mptevents.c $(wildcard mpt/*.h) $(wildcard mpt/mpi/*.h)
	ctags $^
clean:
	-rm -f mptevents mptevents.o tags

.PHONY: all clean
