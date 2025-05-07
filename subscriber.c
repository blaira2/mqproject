// subscriber.c
// subscribe to all publishers, broadcast topic on request
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
#include <liburing.h>

#define BUFFER_SIZE 1024
#define QUEUE_DEPTH 512
#define MAX_TOPIC_LEN 64
#define TOPIC_CAPACITY 16
#define DEFAULT_PORT 5555
#define MICROSERVICE_PORT 4444
#define HEARTBEAT_PORT 5554
#define HEARTBEAT_INTERVAL 3
#define BROADCAST_IP "127.255.255.255"
#define SYSTEM_ID 99

#define TYPE_ACCEPT  0
#define TYPE_READ  1

typedef struct __attribute__((packed)) {
    uint32_t system_id;
    uint16_t advertised_port;
    uint16_t topic_count;
    char topics[TOPIC_CAPACITY][MAX_TOPIC_LEN];
    uint64_t timestamp; // Time when the heartbeat was sent
} heartbeat_t;

typedef struct request{
    int type;
    int client_fd;
    int iov_count;
    struct iovec iov[];
} request;

struct io_uring ring;
static char **subscribed_topics = NULL;
static uint16_t topic_count = 0;
static uint32_t subscriber_id; //to be put in every heartbeat system_id
static uint16_t listen_port; //find available port


// check whether already subscribed
static int is_subscribed(const char *topic) {
    for (size_t i = 0; i < topic_count; ++i) {
        if (strcmp(subscribed_topics[i], topic) == 0)
            return 1;
    }
    return 0;
}

int subscribe_to_topic(const char *topic){
    if (!topic){
        return -1;
    } 
    if (is_subscribed(topic)){
        return 0;
    }

    if (topic_count >= TOPIC_CAPACITY) {
        printf("[SUB] Topics at capacity\n");
    }
    else{
        printf("[SUB] Added subscription for %s\n", topic);
        subscribed_topics[topic_count++] = strdup(topic);
    }

    return 0;
}

uint32_t generate_id() {
    uint32_t pid_part = (uint32_t)getpid();
    uint32_t time_part = (uint32_t)time(NULL);
    return (pid_part * 31) ^ (time_part);
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
        hb.system_id = htonl(subscriber_id);
        hb.timestamp = htobe64(time(NULL));
        hb.advertised_port = htons(listen_port);
        hb.topic_count = htons(topic_count);
        // copy each topic string in
        for (size_t i = 0; i < topic_count; ++i) {
            strncpy(hb.topics[i], subscribed_topics[i], MAX_TOPIC_LEN-1);
            hb.topics[i][MAX_TOPIC_LEN-1] = '\0';
        }

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

int add_accept_request(int server_socket, struct sockaddr_in *client_addr, socklen_t *client_addr_len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, server_socket, (struct sockaddr *) client_addr, client_addr_len, 0);
    struct request *req = malloc(sizeof(*req));
    req->type = TYPE_ACCEPT;
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 0;
}

int add_read_request(int client_socket) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    struct request *req = malloc(sizeof(*req) + sizeof(struct iovec));
    req->iov[0].iov_base = malloc(BUFFER_SIZE);
    req->iov[0].iov_len = BUFFER_SIZE;
    req->type = TYPE_READ;
    req->client_fd = client_socket;
    memset(req->iov[0].iov_base, 0, BUFFER_SIZE);
    io_uring_prep_readv(sqe, client_socket, &req->iov[0], 1, 0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 0;
}

int setup_listen_socket(uint16_t *out_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(0) //ask for a free port
    };
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    socklen_t len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr*)&addr, &len) < 0) {
        perror("[SUB] getsockname fail");
        close(sock);
        return -1;
    }
    *out_port = ntohs(addr.sin_port);

    listen(sock, SOMAXCONN);
    printf("[SUB] Listening on port %u\n", *out_port);
    return sock;
}


void receive_loop(int sock, const char *sub_topic) {
    
    char buffer[BUFFER_SIZE + 1] = {0};
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    //prime io_uring for first accept
    add_accept_request(sock, &address, &addrlen);

    while (1) {

        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        if(!cqe) {
            printf("Completion queue is empty, skipping...\n");
            continue;
            // exit(EXIT_FAILURE);
        }
        struct request *req = (struct request *) cqe->user_data;

        switch (req->type) {
            case TYPE_ACCEPT:
                printf("Accepted client FD: %d\n", cqe->res);
                add_accept_request(sock, &address, &addrlen);
            	add_read_request(cqe->res);
		        break;
            case TYPE_READ:
                int result = cqe->res;
		        
                if (result > 0) {
                    char *msg = req->iov[0].iov_base;
                    msg[result] = '\0';
                    printf("Read from FD: %d: %s\n", req->client_fd, msg);
                    free(msg);
                    add_read_request(req->client_fd);
                    //handle_client_request(req);
                } else {
                    close(req->client_fd);
                }
                break;
        }
        io_uring_cqe_seen(&ring, cqe);

        free(req);

        printf("[SUB] Received message:\n%.*s\n", (int)0, buffer);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <publisher_ip> <topic>\n", argv[0]);
        return 1;
    }
    //generate unique system id
    subscriber_id = generate_id();

    //initalize topic array
    subscribed_topics = malloc(TOPIC_CAPACITY * MAX_TOPIC_LEN);
    subscribe_to_topic(argv[2]);

    //initialize uring
    if(io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0){
        perror("io_uring failed");
        exit(EXIT_FAILURE);
    }
    // set up TCP listen socket
    int listen_fd = setup_listen_socket(&listen_port);

    const char *ip = argv[1];
    //int port = atoi(argv[2]);
    const char *topic = argv[2];

    int hb_sock = setup_heartbeat();
    pthread_t hb_thread;
    pthread_create(&hb_thread, NULL, heartbeat_thread, &hb_sock);
 
    receive_loop(listen_fd, topic);
    puts("Subscriber exit");
    return 0;
}