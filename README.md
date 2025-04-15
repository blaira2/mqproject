# Message Queue Project

A simple publish-subscribe message queue implementation in C.

## Components

- `publisher.c`: Handles message publishing and subscriber management
- `subscriber.c`: Implements the subscriber client

## Building

```bash
# Compile the publisher
gcc -o publisher publisher.c

# Compile the subscriber
gcc -o subscriber subscriber.c
```

## Usage

1. Start the publisher:
```bash
./publisher
```

2. Start one or more subscribers:
```bash
./subscriber
```

3. Publishing messages:
In the publisher terminal, use the format: 
```

## Features

- Support for multiple subscribers
- Topic-based message filtering
- Real-time message delivery