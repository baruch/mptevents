VERSION=1.2
CFLAGS=-O0 -g -Wall -Impt -DVERSION=\"${VERSION}\"

all: mptevents mptevents_offline
mptevents: mptevents.o mptparser.o | Makefile
mptevents_offline: mptevents_offline.o mptparser.o | Makefile
mptevents.o: mptevents.c | Makefile
mptparser.o: mptparser.c | Makefile
tags: mptevents.c $(wildcard mpt/*.h) $(wildcard mpt/mpi/*.h)
	ctags $^
clean:
	-rm -f mptevents mptevents_offline *.o tags

.PHONY: all clean
