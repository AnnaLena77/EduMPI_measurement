# Build with the MPI compiler wrapper of the MPI the program uses:  make CC=mpicc
CC ?= mpicc
CFLAGS ?= -O2 -g -Wall -Wextra
CORE = src/core/store.c src/core/file_sink.c

all: libedumpi.so

libedumpi.so: $(CORE) src/pmpi/init.c src/core/store.h src/core/record.h
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $(CORE) src/pmpi/init.c -lpthread

test_store: tests/test_store.c $(CORE) src/core/store.h src/core/record.h
	cc $(CFLAGS) -pthread -o $@ tests/test_store.c $(CORE)

test_init: tests/test_init.c
	$(CC) $(CFLAGS) -o $@ tests/test_init.c

check: test_store test_init libedumpi.so
	mkdir -p _check && ./test_store _check 8 200000
	cd _check && LD_PRELOAD=$(abspath libedumpi.so) EDUMPI_LOG_DIR=. mpirun -n 2 --oversubscribe ../test_init
	python3 tools/edumpi_dump.py _check/edumpi.0.bin | head -n 5

clean:
	rm -rf libedumpi.so test_store test_init _check
