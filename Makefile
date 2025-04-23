CC=gcc
CFLAGS=-g
LDFLAGS=
MAKEFLAGS=-j2

SUB=subscriber
PUB=publisher

SUB_SRC=$(SUB).c
PUB_SRC=$(PUB).c

all: $(SUB) $(PUB)

subscriber: $(SUB_SRC)
	$(CC) $(CFLAGS) $(SUB_SRC) -o $(SUB) $(LDFLAGS)

publisher: $(PUB_SRC)
	$(CC) $(CFLAGS) $(PUB_SRC) -o $(PUB) $(LDFLAGS)

.PHONY: clean

clean:
	rm subscriber publisher