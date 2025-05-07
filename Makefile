CC=gcc
CFLAGS=-g -O3
LDFLAGS=-luring
MAKEFLAGS=-j$(nproc)

SUB=subscriber
PUB=publisher
URING=publisher_uring
MS=microservice
ZMQ_PUB=zmq_publisher
ZMQ_SUB=zmq_subscriber

SUB_SRC=$(SUB).c
PUB_SRC=$(PUB).c
URING_SRC=$(URING).c
MS_SRC=$(MS).c

all: $(SUB) $(PUB) $(URING) $(MS) $(ZMQ_PUB) $(ZMQ_SUB)

$(SUB): $(SUB).c
	$(CC) $(CFLAGS) $(SUB_SRC) -o $(SUB) $(LDFLAGS)

$(PUB): $(PUB).c
	$(CC) $(CFLAGS) $(PUB_SRC) -o $(PUB)

$(URING): $(URING).c
	$(CC) $(CFLAGS) $(URING_SRC) -o $(URING) $(LDFLAGS)

$(MS): $(MS).c
	$(CC) $(CFLAGS) $(MS_SRC) -o $(MS) $(LDFLAGS)

$(ZMQ_PUB): $(ZMQ_PUB).c
	$(CC) $(CFLAGS) $(ZMQ_PUB).c -o $(ZMQ_PUB) -lzmq

$(ZMQ_SUB): $(ZMQ_SUB).c
	$(CC) $(CFLAGS) $(ZMQ_SUB).c -o $(ZMQ_SUB) -lzmq

.PHONY: clean

clean:
	rm $(SUB) $(PUB) $(URING) $(MS) $(ZMQ_PUB) $(ZMQ_SUB)