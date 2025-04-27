// publisher.c
// publish messages to correct topics
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>
#include <fcntl.h>

#define MAX_SUBS 64
#define MAX_TOPIC_LEN 128
#define MAX_BUFFER_SIZE 1024
#define DEFAULT_PORT 5555
#define HEARTBEAT_PORT 5554


typedef struct {
    int tcp_sock;  // TCP socket file descriptor
    uint32_t ip_addr; // IP address received via heartbeat
    char topic[MAX_TOPIC_LEN];
    int topic_received;
    time_t last_heartbeat;
} subscriber_t;

typedef struct {
    int socket; //publisher socket
    subscriber_t *subs;
} subs_t;



void send_topic_request(int client_sock) {
    const char *msg = "REQ_TOPIC\n";
    send(client_sock, msg, strlen(msg), 0);
}


void remove_subscriber(subscriber_t *subs, int i) {
    printf("[PUB] Subscriber disconnected: fd=%d\n", subs[i].tcp_sock);
    close(subs[i].tcp_sock);
    subs[i].tcp_sock = -1;
    subs[i].topic[0] = 0;
    subs[i].topic_received = 0;
}

int connect_to_subscriber(uint32_t ip_addr) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in sub_addr;
    memset(&sub_addr, 0, sizeof(sub_addr));
    sub_addr.sin_family = AF_INET;
    sub_addr.sin_port = htons(DEFAULT_PORT);
    sub_addr.sin_addr.s_addr = ip_addr;

    if (connect(sock, (struct sockaddr *)&sub_addr, sizeof(sub_addr)) < 0) {
        perror("connect to subscriber");
        close(sock);
        return -1;
    }

    return sock;
}

void handle_messaging(subscriber_t *subs) {
    char input[MAX_BUFFER_SIZE] = {0};

    if (!fgets(input, sizeof(input), stdin)) {
        return;
    }

    char *topic = strtok(input, " ");
    char *msg = strtok(NULL, "\n");

    if (!topic || !msg) {
        printf("Usage: <topic> <message>\n");
        return;
    }

    for (int i = 0; i < MAX_SUBS; i++) {
        if (subs[i].tcp_sock != -1 && subs[i].topic_received &&
            strncmp(subs[i].topic, topic, strlen(topic)) == 0) {
            send(subs[i].tcp_sock, msg, strlen(msg), 0);
            printf("[PUB] Sent to subscriber (slot %d) '%s'\n", i, msg);
        }
    }
}


//broadcast listen
void *subscription_listener_thread(void *arg) {
    subs_t *subset = (subs_t *)arg;
    int hb_sock = subset->socket;
    subscriber_t *subs = subset->subs;

    struct sockaddr_in src_addr;
    socklen_t addr_len;
    char hb_buffer[512];

    printf("[PUB] Heartbeat listener thread started.\n");
    while (1) {
        addr_len = sizeof(src_addr);
        int bytes = recvfrom(hb_sock, hb_buffer, sizeof(hb_buffer) - 1, 0,
                             (struct sockaddr *)&src_addr, &addr_len);

        if (bytes < 0) {
            perror("[PUB] recvfrom error");
            sleep(1);
            continue;
        }
        else if (bytes == 0) {
            //ignore
            continue; 
        }

        hb_buffer[bytes] = '\0';
        printf("[PUB] Received heartbeat: %s\n", hb_buffer);

        uint32_t sender_ip = src_addr.sin_addr.s_addr;

        // Check if subscriber already exists
        int found = 0;
        for (int i = 0; i < MAX_SUBS; i++) {
            if (subs[i].ip_addr != 0 && subs[i].ip_addr == sender_ip) {
                subs[i].last_heartbeat = time(NULL);
                found = 1;
                break;
            }
        }

        if (!found) { // New subscriber
            for (int i = 0; i < MAX_SUBS; i++) {
                if (subs[i].ip_addr == 0) { // empty slot
                    subs[i].ip_addr = sender_ip;
                    subs[i].last_heartbeat = time(NULL);
                    subs[i].topic_received = 0;
                    int new_sock = connect_to_subscriber(sender_ip);
                    if (new_sock >= 0) {
                        subs[i].tcp_sock = new_sock;
                        printf("[PUB] Connected to subscriber %s\n", inet_ntoa(*(struct in_addr *)&sender_ip));
                    } else {
                        printf("[PUB] Failed to make TCP connection to subscriber %s\n", inet_ntoa(*(struct in_addr *)&sender_ip));
                    }
                    break;
                }
            }
        }
    }

    printf("[PUB] Exiting heartbeat listener thread.\n");
    free(subset); // Only if you malloc'd it
    return NULL;
}



void run_publisher_loop(int server_sock, subscriber_t *subs) {
    fd_set read_fds;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);
        int max_fd = server_sock;

        for (int i = 0; i < MAX_SUBS; i++) {
            if (subs[i].tcp_sock != -1) {
                FD_SET(subs[i].tcp_sock, &read_fds);
                if (subs[i].tcp_sock > max_fd) {
                    max_fd = subs[i].tcp_sock;
                }
            }
        }

        FD_SET(STDIN_FILENO, &read_fds);
        if (STDIN_FILENO > max_fd) {
            max_fd = STDIN_FILENO;
        }

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(server_sock, &read_fds)) {
            //don't accept incoming TCP
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            handle_messaging(subs);
        }
    }
}

int main() {
    // Setup TCP socket for publishing messages
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
        .sin_port = htons(DEFAULT_PORT),
    };

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(server_sock, MAX_SUBS);
    printf("[PUB] Listening on port %d...\n", DEFAULT_PORT);

    // Setup UDP heartbeat socket for listening for subscribers
    int hb_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (hb_sock < 0) {
        perror("heartbeat socket");
        return 1;
    }

    setsockopt(hb_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(hb_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)); // allow broadcast reception

    struct sockaddr_in hb_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY, 
        .sin_port = htons(HEARTBEAT_PORT),
    };

    if (bind(hb_sock, (struct sockaddr *)&hb_addr, sizeof(hb_addr)) < 0) {
        perror("heartbeat bind");
        return 1;
    }

    // Initialize subscribers list
    subscriber_t subs[MAX_SUBS];
    for (int i = 0; i < MAX_SUBS; i++) {
        subs[i].tcp_sock = -1;
    }

    // Allocate and set up subscription listener
    subs_t *subset = malloc(sizeof(subs_t));
    if (!subset) {
        perror("malloc");
        return 1;
    }
    subset->socket = hb_sock;
    subset->subs = subs;

    pthread_t listener_thread;
    if (pthread_create(&listener_thread, NULL, subscription_listener_thread, subset) != 0) {
        perror("pthread_create");
        free(subset);
        return 1;
    }

    pthread_detach(listener_thread); // auto-cleanup thread when it exits
    run_publisher_loop(server_sock, subs);// input publisher loop

    free(subset);
    close(server_sock);
    close(hb_sock);
    return 0;
}