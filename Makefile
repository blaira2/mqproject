CC=gcc
CFLAGS=-g
LDFLAGS=
MAKEFLAGS=-j2

SUB=subscriber
PUB=publisher

SUB_SRC=$(SUB).c
PUB_SRC=$(SUB).c

all: $(SUB) $(PUB)

subscriber: 
	$(CC) $(CFLAGS) $(SUB_SRC) -o $(SUB) $(LDFLAGS)

publisher: 
	$(CC) $(CFLAGS) $(PUB_SRC) -o $(PUB) $(LDFLAGS)

.PHONY: clean

clean:
	rm subscriber publisher