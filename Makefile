CC = gcc
CFLAGS = -Wall -Wextra

all: publisher subscriber

publisher: publisher.c
	$(CC) $(CFLAGS) -o publisher publisher.c

subscriber: subscriber.c
	$(CC) $(CFLAGS) -o subscriber subscriber.c

clean:
	rm -f publisher subscriber *.o

.PHONY: all clean 