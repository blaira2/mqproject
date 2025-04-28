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
#define HEARTBEAT_PORT 5554
#define HEARTBEAT_INTERVAL 3
#define BROADCAST_IP "127.255.255.255"
#define SYSTEM_ID 99

typedef struct __attribute__((packed)) {
    uint32_t system_id;   
    uint32_t subscriber_id;
    uint64_t timestamp; // Time when the heartbeat was sent
} heartbeat_t;

//CREATE TCP socket
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

    // char* test = "w";
    // if(send(sock,test,strlen(test),0) < 0){
    //     perror("send");
    //     close(sock);
    //     exit(1);
    // }

    return sock;
}

//send initial subscription message to publisher
int subscribe(int sock, const char topic[MAX_TOPIC_LEN]){
    if(send(sock,topic,strnlen(topic,MAX_TOPIC_LEN),0) < 0){
        perror("send");
        close(sock);
        exit(1);
    }
    return 0;
}

// Create UDP socket
int setup_heartbeat(){
    int hb_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (hb_sock < 0) {
        perror("socket");
        exit(1);
    }
         // Enable broadcast option
    int broadcast_enable = 1;
    if (setsockopt(hb_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) == -1) {
        perror("setsockopt - SO_BROADCAST");
        close(hb_sock);
        exit(1);
    }

    return hb_sock;
}

//broadcast heartbeat
void *heartbeat_thread(void *arg){
    int hb_sock = *(int *)arg;
    struct sockaddr_in broadcast_addr;
    // Configure broadcast address
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(HEARTBEAT_PORT);

    if (inet_pton(AF_INET, BROADCAST_IP, &broadcast_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(hb_sock);
        exit(EXIT_FAILURE);
    }
    printf("[SUB] Heartbeat thread started. Broadcasting heartbeat to %s:%d...\n",
           BROADCAST_IP, HEARTBEAT_PORT);
    
    while (1) {
        heartbeat_t hb;
        hb.system_id = htonl(SYSTEM_ID);
        hb.subscriber_id = htonl(3); 
        hb.timestamp = htobe64(time(NULL)); 


        int beat = sendto(hb_sock, &hb, sizeof(hb), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        if (beat < 0) {
            perror("Failed to beat.");
        }
        else{
            //printf("[SUB] beat.\n");////
        }
        sleep(HEARTBEAT_INTERVAL);
    }

}


void receive_loop(int sock, const char *sub_topic) {
    char buffer[MAX_BUFFER_SIZE + 1] = {0};
    while (1) {
        puts("Receive loop");
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = recv(sock, buffer, MAX_BUFFER_SIZE, 0);
        if (n <= 0) {
            perror("recv");
            break;
        }

        // Check if this is a topic request .
        if (strncmp(buffer, "REQ_TOPIC", 9) == 0) {
            printf("[SUB] Received REQ_TOPIC, sending subscription '%s'\n", sub_topic);
            send(sock, sub_topic, strlen(sub_topic), 0);
            continue;
        }

        // Normal message
        printf("[SUB] Received message:\n%.*s\n", (int)n, buffer);
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
    int hb_sock = setup_heartbeat();
    pthread_t hb_thread;
    pthread_create(&hb_thread, NULL, heartbeat_thread, &hb_sock);
 
    printf("[SUB] Connected to %s:%d, will subscribe to '%s'\n", ip, port, topic);
    subscribe(sock,topic);
    receive_loop(sock, topic);
    puts("Subscriber exit");
    return 0;
}