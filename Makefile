VERSION=1.0
CFLAGS=-O3 -g -Wall -Impt -DVERSION=\"${VERSION}\"

all: mptevents
mptevents: mptevents.o | Makefile
mptevents.o: mptevents.c | Makefile
tags: mptevents.c $(wildcard mpt/*.h) $(wildcard mpt/mpi/*.h)
	ctags $^
clean:
	-rm -f mptevents mptevents.o tags

.PHONY: all clean
