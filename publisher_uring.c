#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <ctype.h>

#include <liburing.h>

#define PORT 5555

#define QUEUE_DEPTH             256
#define READ_SZ                 1024

#define EVENT_TYPE_ACCEPT       0
#define EVENT_TYPE_READ         1
#define EVENT_TYPE_WRITE        2

struct request {
    int event_type;     //accept connection, read from fd, write to fd
    int iovec_count;
    int client_socket;      //fd for client socket 
    struct iovec iov[];
};

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

}