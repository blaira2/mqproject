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

#define MAX_SUBS 128
#define MAX_TOPIC_LEN 64
#define TOPIC_CAPACITY 16
#define MAX_BUFFER_SIZE 1024
#define DEFAULT_PORT 5555
#define HEARTBEAT_PORT 5554

typedef struct __attribute__((packed)) {
    uint32_t system_id;   
    uint32_t subscriber_id;
    uint16_t topic_count; 
    char topics[TOPIC_CAPACITY][MAX_TOPIC_LEN];
    uint64_t timestamp; // Time when the heartbeat was sent
} heartbeat_t;

typedef struct {
    int tcp_sock;  // TCP socket file descriptor
    uint32_t ip_addr; 
    char topics[TOPIC_CAPACITY][MAX_TOPIC_LEN];
    int topic_count;
    int topic_received;
    time_t last_heartbeat; //healthcheck
} subscriber_t;

typedef struct {
    int socket; //publisher socket
    subscriber_t *subs;
} subs_t;



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

//Debug only
void debug_subscription_matching(subscriber_t *subs, const char *topic, const char *msg) {
    printf("=== Debug: publishing message on topic '%s': \"%s\" ===\n",
           topic, msg);

    for (int i = 0; i < MAX_SUBS; i++) {
        subscriber_t *sub = &subs[i];
        if (sub->tcp_sock < 0) continue;  // skip unused slots

        // Convert IP to dotted-quad
        struct in_addr in;
        in.s_addr = sub->ip_addr;
        const char *ip_str = inet_ntoa(in);

        printf("Subscriber[%2d]: fd=%d, ip=%s, topic_received=%s, topic_count=%d\n",
               i,
               sub->tcp_sock,
               ip_str,
               sub->topic_received ? "true" : "false",
               sub->topic_count);

        // List its subscribed topics
        printf("    Topics:");
        for (int t = 0; t < sub->topic_count; t++) {
            printf(" '%s'", sub->topics[t]);
        }
        printf("\n");

        // Check for match
        if (sub->topic_received && sub->topic_count > 0) {
            int match = 0;
            for (int t = 0; t < sub->topic_count; t++) {
                if (strcmp(topic, sub->topics[t]) == 0) {
                    match = 1;
                    break;
                }
            }
            printf("    -> %s\n\n",
                   match ? "MATCH: will send" : "NO MATCH: skipping");
        } else {
            printf("    -> SKIPPING: no topics registered yet\n\n");
        }
    }
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
    if( strlen(topic) > MAX_TOPIC_LEN){
        printf("Invalid topic\n");
        return;
    }

    debug_subscription_matching(subs, topic, msg); //print out a bunch of stuff

    // check all subs/topics
    for (int i = 0; i < MAX_SUBS; i++) {
        if (subs[i].tcp_sock >= 0 && subs[i].topic_received) {
            for (int t = 0; t < subs[i].topic_count; t++) {
                if (strcmp(topic, subs[i].topics[t]) == 0) {
                    send(subs[i].tcp_sock, msg, strlen(msg), 0);
                }
            }
        }
    }
}


// Broadcast listen (UDP) for heartbeats.
// Using heartbeats to determine each subscribers topics
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

        //extract heartbeat
        heartbeat_t *hb = (heartbeat_t*)hb_buffer;
        uint32_t sender_ip = src_addr.sin_addr.s_addr;
        uint16_t count = ntohs(hb->topic_count);

        //check in subscriber array
        int slot = -1;
        for (int i = 0; i < MAX_SUBS; i++) {
            if (subs[i].ip_addr == sender_ip) {
                slot = i; //already exists
                break;
            }
            if (slot < 0 && subs[i].ip_addr == 0) {
                slot = i; //new subscriber
            }
        }
        if (slot < 0) {
            continue; //if no free slots ignore
        }
        // Update heartbeat timestamp
        subs[slot].ip_addr         = sender_ip;
        subs[slot].last_heartbeat  = time(NULL);

         // fill in new subscriber struct details
        for (int t = 0; t < count; t++) {
            strncpy(subs[slot].topics[t],
                    hb->topics[t],
                    MAX_TOPIC_LEN);
            subs[slot].topics[t][MAX_TOPIC_LEN-1] = '\0';
        }
        subs[slot].topic_count    = count;
        subs[slot].topic_received = (count > 0);

        // If this is a new subscriber, connect (TCP)
        if (subs[slot].tcp_sock < 0) {
            int sock = connect_to_subscriber(sender_ip);
            if (sock >= 0) {
                subs[slot].tcp_sock = sock;
                printf("[PUB] Connected to subscriber %s on %d topics\n",
                       inet_ntoa(*(struct in_addr *)&sender_ip),
                       count);
            } else {
                printf("[PUB] Failed to connect to %s\n",
                       inet_ntoa(*(struct in_addr *)&sender_ip));
            }
        }
    }

    printf("[PUB] Exiting heartbeat listener thread.\n");

    return NULL;
}


void run_publisher_loop(int server_sock, subscriber_t *subs) {

    while (1) {
        //just handles system input for now
        handle_messaging(subs);
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
    printf("[PUB] Listening for heartbeats on %d...\n", HEARTBEAT_PORT);

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

    pthread_detach(listener_thread); 
    run_publisher_loop(server_sock, subs);// input publisher loop

    free(subset);
    close(server_sock);
    close(hb_sock);
    return 0;
}