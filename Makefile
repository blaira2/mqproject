CC=gcc
CFLAGS=-g
LDFLAGS=-luring
MAKEFLAGS=-j$(nproc)

SUB=subscriber
PUB=publisher
URING=publisher_uring

SUB_SRC=$(SUB).c
PUB_SRC=$(PUB).c
URING_SRC=$(URING).c

all: $(SUB) $(PUB) $(URING)

subscriber: $(SUB_SRC)
	$(CC) $(CFLAGS) $(SUB_SRC) -o $(SUB)

publisher: $(PUB_SRC)
	$(CC) $(CFLAGS) $(PUB_SRC) -o $(PUB)

publisher_uring: $(URING_SRC)
	$(CC) $(CFLAGS) $(URING_SRC) -o $(URING) $(LDFLAGS)

.PHONY: clean

clean:
	rm $(SUB) $(PUB) $(URING)