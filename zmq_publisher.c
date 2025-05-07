// publisher_zmq.c
#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ENDPOINT "tcp://*:5556"
#define MAX_LINE 1024

int main() {
    void *ctx = zmq_ctx_new();
    void *pub = zmq_socket(ctx, ZMQ_PUB);
    if (zmq_bind(pub, ENDPOINT) != 0) {
        perror("zmq_bind");
        return 1;
    }
    printf("ZeroMQ PUB bound at %s\n", ENDPOINT);

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), stdin)) {
        // Expect input: "<topic> <message>\n"
        char *topic = strtok(line, " ");
        char *msg   = strtok(NULL, "\n");
        if (!topic || !msg) {
            fprintf(stderr, "Usage: <topic> <message>\n");
            continue;
        }
        // send topic frame
        if (zmq_send(pub, topic, strlen(topic), ZMQ_SNDMORE) < 0) {
            perror("zmq_send topic");
            break;
        }
        // send payload frame
        if (zmq_send(pub, msg, strlen(msg), 0) < 0) {
            perror("zmq_send msg");
            break;
        }
    }

    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    return 0;
}
