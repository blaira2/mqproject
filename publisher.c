// publisher.c
// publish messages to correct topics
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_SUBS 64
#define MAX_TOPIC_LEN 128
#define MAX_BUFFER_SIZE 1024
#define PORT 5555

typedef struct {
    int sock;
    char topic[MAX_TOPIC_LEN];
    int topic_received;
} subscriber_t;

void send_topic_request(int client_sock) {
    const char *msg = "REQ_TOPIC\n";
    send(client_sock, msg, strlen(msg), 0);
}

void remove_subscriber(subscriber_t *subs, int i) {
    printf("[PUB] Subscriber disconnected: fd=%d\n", subs[i].sock);
    close(subs[i].sock);
    subs[i].sock = -1;
    subs[i].topic[0] = 0;
    subs[i].topic_received = 0;
}

void run_publisher_loop(int server_sock, subscriber_t *subs) {
    fd_set read_fds;
    char input[MAX_BUFFER_SIZE];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);
        int max_fd = server_sock;

        for (int i = 0; i < MAX_SUBS; i++) {
            if (subs[i].sock != -1) {
                FD_SET(subs[i].sock, &read_fds);
                if (subs[i].sock > max_fd) max_fd = subs[i].sock;
            }
        }

        FD_SET(STDIN_FILENO, &read_fds);
        if (STDIN_FILENO > max_fd) max_fd = STDIN_FILENO;

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(server_sock, &read_fds)) {
            // New subscriber
            int client_sock = accept(server_sock, NULL, NULL);
            if (client_sock < 0) {
                perror("accept");
                continue;
            }

            int inserted = 0;
            for (int i = 0; i < MAX_SUBS; i++) {
                if (subs[i].sock == -1) {
                    subs[i].sock = client_sock;
                    subs[i].topic_received = 0;
                    send_topic_request(client_sock);
                    printf("[PUB] New subscriber fd=%d\n", client_sock);
                    inserted = 1;
                    break;
                }
            }
            if (!inserted) {
                printf("[PUB] Too many subscribers. Rejecting fd=%d\n", client_sock);
                close(client_sock);
            }
        }

        for (int i = 0; i < MAX_SUBS; i++) {
            if (subs[i].sock != -1 && FD_ISSET(subs[i].sock, &read_fds)) {
                if (!subs[i].topic_received) {
                    // Expect topic response
                    char topic_buf[MAX_TOPIC_LEN] = {0};
                    int n = recv(subs[i].sock, topic_buf, sizeof(topic_buf) - 1, 0);
                    if (n <= 0) {
                        remove_subscriber(subs, i);
                    } else {
                        topic_buf[n] = '\0';
                        strncpy(subs[i].topic, topic_buf, MAX_TOPIC_LEN);
                        subs[i].topic_received = 1;
                        printf("[PUB] Subscriber fd=%d subscribed to '%s'\n", subs[i].sock, subs[i].topic);
                    }
                } else {
                    // Unexpected message
                    char dump[256];
                    recv(subs[i].sock, dump, sizeof(dump), 0); // just discard
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            // Read from user: format = <topic> <message>
            memset(input, 0, sizeof(input));
            if (!fgets(input, sizeof(input), stdin)) continue;

            char *topic = strtok(input, " ");
            char *msg = strtok(NULL, "\n");
            if (!topic || !msg) {
                printf("Usage: <topic> <message>\n");
                continue;
            }

            for (int i = 0; i < MAX_SUBS; i++) {
                if (subs[i].sock != -1 && subs[i].topic_received &&
                    strncmp(subs[i].topic, topic, strlen(topic)) == 0) {
                    send(subs[i].sock, msg, strlen(msg), 0);
                    printf("[PUB] Sent to fd=%d: '%s'\n", subs[i].sock, msg);
                }
            }
        }
    }
}

int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT),
    };

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(server_sock, MAX_SUBS);
    printf("[PUB] Listening on port %d...\n", PORT);

    subscriber_t subs[MAX_SUBS];
    for (int i = 0; i < MAX_SUBS; i++) subs[i].sock = -1;

    run_publisher_loop(server_sock, subs);

    close(server_sock);
    return 0;
}