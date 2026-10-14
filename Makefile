CC = gcc
CPPFLAGS += -D_GNU_SOURCE -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
COMMON = src/dataset.c src/ipc_utils.c
HEADERS = $(wildcard include/*.h)
SHM = src/shm_utils.c
INPUT = src/console_input.c
SHM_TEST_FLAGS = -DSHM_TEST_BUILD -DSHM_CLIENT_LOCK='"shm-client.lock"'
TEST_PATHS = -DFIFO_REQ='"request.fifo"' -DFIFO_RESP='"response.fifo"' -DFIFO_LOCK='"server.lock"'

.PHONY: all test clean
all: servidor_fifo cliente_fifo servidor_shm cliente_shm

servidor_fifo: src/servidor_fifo.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/servidor_fifo.c $(COMMON) $(LDFLAGS) $(LDLIBS) -o $@
cliente_fifo: src/cliente_fifo.c src/ipc_utils.c $(INPUT) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cliente_fifo.c src/ipc_utils.c $(INPUT) $(LDFLAGS) $(LDLIBS) -o $@
servidor_shm: src/servidor_shm.c src/dataset.c $(SHM) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/servidor_shm.c src/dataset.c $(SHM) $(LDFLAGS) $(LDLIBS) -pthread -o $@
cliente_shm: src/cliente_shm.c $(SHM) $(INPUT) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cliente_shm.c $(SHM) $(INPUT) $(LDFLAGS) $(LDLIBS) -pthread -o $@
bin:
	mkdir -p $@
bin/test_parser: tests/test_parser.c src/dataset.c $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_parser.c src/dataset.c $(LDFLAGS) $(LDLIBS) -o $@
bin/test_dataset: tests/test_dataset.c src/dataset.c $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_dataset.c src/dataset.c -Wl,--wrap=getline,--wrap=__getdelim,--wrap=ferror,--wrap=fclose $(LDFLAGS) $(LDLIBS) -o $@
bin/test_ipc: tests/test_ipc.c src/ipc_utils.c $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ipc.c src/ipc_utils.c -Wl,--wrap=ppoll $(LDFLAGS) $(LDLIBS) -o $@
bin/servidor_fifo_test: src/servidor_fifo.c $(COMMON) $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_PATHS) src/servidor_fifo.c $(COMMON) $(LDFLAGS) $(LDLIBS) -o $@
bin/cliente_fifo_test: src/cliente_fifo.c src/ipc_utils.c $(INPUT) $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_PATHS) src/cliente_fifo.c src/ipc_utils.c $(INPUT) $(LDFLAGS) $(LDLIBS) -o $@
bin/servidor_shm_test: src/servidor_shm.c src/dataset.c $(SHM) $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SHM_TEST_FLAGS) src/servidor_shm.c src/dataset.c $(SHM) $(LDFLAGS) $(LDLIBS) -pthread -o $@
bin/cliente_shm_test: src/cliente_shm.c $(SHM) $(INPUT) $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SHM_TEST_FLAGS) src/cliente_shm.c $(SHM) $(INPUT) $(LDFLAGS) $(LDLIBS) -pthread -o $@
bin/shm_probe: tests/shm_probe.c $(SHM) $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SHM_TEST_FLAGS) $< $(SHM) -pthread -o $@
bin/shm_faults.so: tests/shm_faults.c | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $< -ldl -o $@

test: all bin/test_parser bin/test_dataset bin/test_ipc bin/servidor_fifo_test bin/cliente_fifo_test bin/servidor_shm_test bin/cliente_shm_test bin/shm_probe bin/shm_faults.so
	./bin/test_parser
	./bin/test_dataset
	./bin/test_ipc
	python3 tests/test_fifo.py
	python3 tests/test_shm.py
	python3 tests/test_cleanup.py

clean:
	rm -f servidor_fifo cliente_fifo servidor_shm cliente_shm bin/test_parser bin/test_dataset bin/test_ipc bin/servidor_fifo_test bin/cliente_fifo_test bin/servidor_shm_test bin/cliente_shm_test bin/shm_probe bin/shm_faults.so
