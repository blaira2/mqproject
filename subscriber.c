// subscriber.c
// subscribe to all publishers, boradcast topic on request
// receives with io_uring
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_TOPIC_LEN 128
#define DEFAULT_PORT 5555

int connect_to_publisher(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        exit(1);
    }

    return sock;
}


void receive_loop(int sock, const char *sub_topic) {
    char buffer[MAX_BUFFER_SIZE + 1] = {0};

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = recv(sock, buffer, MAX_BUFFER_SIZE, 0);
        if (n <= 0) {
            perror("recv");
            break;
        }

        // Check if this is a topic request
        if (strncmp(buffer, "REQ_TOPIC", 9) == 0) {
            printf("[SUB] Received REQ_TOPIC, sending subscription '%s'\n", sub_topic);
            send(sock, sub_topic, strlen(sub_topic), 0);
            continue;
        }

        // Normal message
        printf("[SUB] Received message: %.*s\n", (int)n, buffer);
    }

    close(sock);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <publisher_ip> <port> <topic>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];

    int sock = connect_to_publisher(ip, port);
    printf("[SUB] Connected to %s:%d, will subscribe to '%s'\n", ip, port, topic);

    receive_loop(sock, topic);
    return 0;
}