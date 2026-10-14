CC = gcc
CPPFLAGS += -D_GNU_SOURCE -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
COMMON = src/dataset.c src/ipc_utils.c
HEADERS = include/dataset.h include/ipc_utils.h include/protocol.h
TEST_PATHS = -DFIFO_REQ='"request.fifo"' -DFIFO_RESP='"response.fifo"' -DFIFO_LOCK='"server.lock"'

.PHONY: all test clean
all: servidor_fifo cliente_fifo

servidor_fifo: src/servidor_fifo.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/servidor_fifo.c $(COMMON) $(LDFLAGS) $(LDLIBS) -o $@
cliente_fifo: src/cliente_fifo.c src/ipc_utils.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cliente_fifo.c src/ipc_utils.c $(LDFLAGS) $(LDLIBS) -o $@
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
bin/cliente_fifo_test: src/cliente_fifo.c src/ipc_utils.c $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_PATHS) src/cliente_fifo.c src/ipc_utils.c $(LDFLAGS) $(LDLIBS) -o $@

test: all bin/test_parser bin/test_dataset bin/test_ipc bin/servidor_fifo_test bin/cliente_fifo_test
	./bin/test_parser
	./bin/test_dataset
	./bin/test_ipc
	python3 tests/test_fifo.py

clean:
	rm -f servidor_fifo cliente_fifo bin/test_parser bin/test_dataset bin/test_ipc bin/servidor_fifo_test bin/cliente_fifo_test
