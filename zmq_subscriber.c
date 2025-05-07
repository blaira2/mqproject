// subscriber_zmq.c
#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_TOPIC 256
#define MAX_MSG   1024
#define PORT      5556

//like ./subscriber_zmq tcp://localhost:5556 news
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <publisher_endpoint> <topic_prefix>\n", argv[0]);
        return 1;
    }
    const char *endpoint = argv[1];
    const char *filter   = argv[2];

    void *ctx = zmq_ctx_new();
    void *sub = zmq_socket(ctx, ZMQ_SUB);
    if (zmq_connect(sub, endpoint) != 0) {
        perror("zmq_connect");
        return 1;
    }
    // subscribe by prefix
    if (zmq_setsockopt(sub, ZMQ_SUBSCRIBE, filter, strlen(filter)) != 0) {
        perror("zmq_setsockopt");
        return 1;
    }
    printf("ZeroMQ SUB connected to %s, filter=\"%s\"\n", endpoint, filter);

    char topic[MAX_TOPIC], msg[MAX_MSG];
    while (1) {
        int n = zmq_recv(sub, topic, sizeof(topic)-1, 0);
        if (n < 0) break;
        topic[n] = '\0';

        n = zmq_recv(sub, msg, sizeof(msg)-1, 0);
        if (n < 0) break;
        msg[n] = '\0';

        printf("[%s] %s\n", topic, msg);
        fflush(stdout);
    }

    zmq_close(sub);
    zmq_ctx_destroy(ctx);
    return 0;
}
