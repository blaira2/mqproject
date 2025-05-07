CC=gcc
CFLAGS=-g -O3
LDFLAGS=-luring
MAKEFLAGS=-j$(nproc)

SUB=subscriber
PUB=publisher
URING=publisher_uring
MS=microservice

SUB_SRC=$(SUB).c
PUB_SRC=$(PUB).c
URING_SRC=$(URING).c
MS_SRC=$(MS).c

all: $(SUB) $(PUB) $(URING) $(MS)

$(SUB): $(SUB_SRC)
	$(CC) $(CFLAGS) $(SUB_SRC) -o $(SUB) $(LDFLAGS)

$(PUB): $(PUB_SRC)
	$(CC) $(CFLAGS) $(PUB_SRC) -o $(PUB)

$(URING): $(URING_SRC)
	$(CC) $(CFLAGS) $(URING_SRC) -o $(URING) $(LDFLAGS)

$(MS): $(MS_SRC)
	$(CC) $(CFLAGS) $(MS_SRC) -o $(MS) $(LDFLAGS)

.PHONY: clean

clean:
	rm $(SUB) $(PUB) $(URING) $(MS)