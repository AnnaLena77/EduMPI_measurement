# Build with the MPI compiler wrapper of the MPI the program uses:  make CC=mpicc
CC ?= mpicc
CFLAGS ?= -O2 -g -Wall -Wextra
CORE = src/core/store.c src/core/file_sink.c src/core/tee_sink.c src/core/pg_sink.c
# PostgreSQL sink:  make PG=1 PQINC=<libpq include dir> PQLIB=<libpq lib dir>
ifdef PG
CFLAGS += -DEDUMPI_WITH_PG -I$(PQINC)
LDPQ = -L$(PQLIB) -Wl,-rpath,$(PQLIB) -lpq
endif

all: libedumpi.so

PMPI = src/pmpi/init.c src/pmpi/track.c src/pmpi/wrappers.c src/mpit/mpit.c

src/pmpi/wrappers.c: tools/gen_pmpi.py
	python3 tools/gen_pmpi.py $@

libedumpi.so: $(CORE) $(PMPI) src/pmpi/wrap.h src/core/store.h src/core/record.h
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $(CORE) $(PMPI) -lpthread $(LDPQ)

test_store: tests/test_store.c $(CORE) src/core/store.h src/core/record.h
	cc $(CFLAGS) -pthread -o $@ tests/test_store.c $(CORE) $(LDPQ)

test_init: tests/test_init.c
	$(CC) $(CFLAGS) -o $@ tests/test_init.c

test_calls: tests/test_calls.c
	$(CC) $(CFLAGS) -o $@ tests/test_calls.c

bench: tests/bench.c
	$(CC) -O2 -o $@ tests/bench.c

test_nbc: tests/test_nbc.c
	$(CC) $(CFLAGS) -o $@ tests/test_nbc.c

test_late: tests/test_late.c
	$(CC) $(CFLAGS) -o $@ tests/test_late.c

check: test_store test_init test_calls libedumpi.so
	mkdir -p _check && ./test_store _check 8 200000
	cd _check && LD_PRELOAD=$(abspath libedumpi.so) EDUMPI_LOG_DIR=. mpirun -n 2 --oversubscribe ../test_init
	rm -f _check/edumpi.*.bin; cd _check && LD_PRELOAD=$(abspath libedumpi.so) EDUMPI_LOG_DIR=. mpirun -n 4 --oversubscribe ../test_calls
	python3 tests/check_calls.py _check 4

clean:
	rm -rf libedumpi.so test_store test_init test_calls test_late test_nbc bench _check
