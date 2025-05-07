# Message Queue Project

A simple publish-subscribe message queue implementation in C.

## Components

- `publisher.c`: Handles message publishing and subscriber management
- `subscriber.c`: Implements the subscriber client

## Building

```bash
# Run make to compile them both
make

# Or compile them separately
# Compile the publisher
gcc -o publisher publisher.c

# Compile the subscriber
gcc -o subscriber subscriber.c
```

## Usage

1. Start one or more publishers:
```bash
./publisher
```

2. Start one or more subscribers:
```bash
./subscriber <topic>
```

3. Publishing messages:
In the publisher terminal, use the format: 
```bash
<topic> <message>
```

## Features

- Support for multiple subscribers
- Topic-based message filtering
- subscribers UDP heartbeat for discovery
- Support for multiple publishers
- subscribers to discover new publishers
- publishers clean up missing subscribers 
- subscriber queues

