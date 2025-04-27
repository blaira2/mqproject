#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>

#include <liburing.h>
#include <pthread.h>

#define PORT 5555
#define MAX_SUBS 64
#define MAX_TOPIC_LEN 128
#define MAX_BUFFER_SIZE 1024
#define HEARTBEAT_PORT 5554

#define QUEUE_DEPTH             256
#define READ_SZ                 1024

#define EVENT_TYPE_ACCEPT       0
#define EVENT_TYPE_READ         1
#define EVENT_TYPE_WRITE        2

volatile int running = 1;
void sigint_handler(){
    running = 0;
}

struct request {
    int event_type;     //accept connection, read from fd, write to fd
    int iovec_count;
    int client_socket;      //fd for client socket 
    struct iovec iov[];
};

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

struct io_uring ring;

int add_accept_request(int server_socket, struct sockaddr_in* client_addr, socklen_t* client_addr_len) {
    //sqe = submission queue entry
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, server_socket, (struct sockaddr*) client_addr, client_addr_len, 0);
    //prepare and submit accept request
    struct request* req = malloc(sizeof(*req));
    req->event_type = EVENT_TYPE_ACCEPT;
    io_uring_sqe_set_data(sqe, req);
    return io_uring_submit(&ring);
}

int add_read_request(int client_socket) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    struct request* req = malloc(sizeof(*req) + sizeof(struct iovec));
    req->iov[0].iov_base = calloc(READ_SZ,1);
    req->iov[0].iov_len = READ_SZ;
    req->client_socket = client_socket;
    //prepare and submit read request
    req->event_type = EVENT_TYPE_READ;
    io_uring_prep_readv(sqe, client_socket, &req->iov[0], 1, 0);    //eventually calls read
    io_uring_sqe_set_data(sqe, req);
    return io_uring_submit(&ring);
}

int add_write_request(struct request* req) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    //prepare and submit write request
    req->event_type = EVENT_TYPE_WRITE;
    io_uring_prep_writev(sqe, req->client_socket, req->iov, req->iovec_count, 0);
    io_uring_sqe_set_data(sqe, req);
    return io_uring_submit(&ring);
}

int main(int argc, char* argv){
    int hb_sock;    
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT),
    };
    int opt = 1;

    if((hb_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(hb_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT | SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(hb_sock, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(hb_sock, 200) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("[PUB] Listening on port %d...\n",PORT);

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
    signal(SIGINT,sigint_handler);

    //io_uring setup
    struct io_uring_cqe* cqe;
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    int ret = 0;
    while(running){
        //wait for completion queue entries
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            perror("io_uring_wait_cqe");
            exit(EXIT_FAILURE);
        }
        struct request* req = (struct request*) cqe->user_data;
        if (cqe->res < 0) { //return value for this event
            continue;
        }
    }

    free(subset);
    return 0;
}