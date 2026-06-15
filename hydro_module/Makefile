CC=gcc
CFLAGS=-O3 -Wall -Wextra -fPIC

all: hydrodb.so

hydrods_engine.o: hydrods_engine.c hydrods_engine.h
	$(CC) $(CFLAGS) -c hydrods_engine.c -o hydrods_engine.o

hydro_module.o: hydro_module.c
	$(CC) $(CFLAGS) -c hydro_module.c -o hydro_module.o

hydrodb.so: hydrods_engine.o hydro_module.o
	ld -o hydrodb.so hydrods_engine.o hydro_module.o -shared -Bsymbolic -lc

clean:
	rm -f *.o *.so
