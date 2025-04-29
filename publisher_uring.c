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
#include <time.h>

#define PORT 5555
#define MAX_SUBS 64
#define MAX_TOPIC_LEN 512
#define MAX_BUFFER_SIZE 1024
#define HEARTBEAT_PORT 5554

#define QUEUE_DEPTH             256
#define READ_SZ                 1024

#define EVENT_TYPE_ACCEPT       0
#define EVENT_TYPE_READ         1
#define EVENT_TYPE_WRITE        2

#define RESPONSE "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-length:15\r\n\r\nHello, world!\r\n"

struct topic_tree {
    int num_children;
    char* topic;
    struct topic_tree* child[];
};

/*
Topic tree:
{toplevel}
    {child1}
        {grand1}
        {grand2}
        {grand3}
    {child2}
        {grand4}
    {child3}
        {grand5}
        {grand6}
*/

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
    struct request* req = malloc(sizeof(struct request));
    req->event_type = EVENT_TYPE_ACCEPT;
    io_uring_sqe_set_data(sqe, req);
    return io_uring_submit(&ring);
}

int add_read_request(int client_socket) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    struct request* req = malloc(sizeof(struct request) + sizeof(struct iovec));
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

//generate (random topic) headers and body for a request
void generate_request_data(char** headers, char** body, struct topic_tree* topics){
    char* new_headers = calloc(MAX_TOPIC_LEN,1);
    int num = (rand() % 5) + 1;
    // for(int i = 0; i < 30; i++){
    //     num = rand() % 30;
    //     printf("num %d: %d\n",i,num);
    // }
    //copy starting topic into headers
    strcpy(new_headers,"{");
    strcat(new_headers,topics->topic);
    
    if(num == 1){
        strcat(new_headers,":child1,child2:grand4");
    }
    if(num == 2){
        strcat(new_headers,":child2");
    }
    if(num == 3){
        strcat(new_headers,":child1:grand2");
    }
    if(num == 4){
        strcat(new_headers,":child2,child3:grand5");
    }
    //programatically generate random topic strings
    //may fix later since it's buggy now
    // 4/5 chance of including children
    // if(num < 4){
    //     strcat(new_headers,":");
    //     for(int i = 0; i < topics->num_children; i++){
    //         struct topic_tree* current = topics->child[i];
    //         for(int j = 0; j < current->num_children; j++){
    //             num = (rand() % 5) + 1;
    //             if(num < 6){

    //             }
    //         }
    //         if(current && current->topic){
    //             strcat(new_headers,current->topic);
    //             num = (rand() % 5) + 1;
    //         }

    //     }
    // }


    //closing curly brace
    strcat(new_headers,"}");
    if(headers != NULL){
        memcpy(*headers,new_headers,strlen(new_headers));
    }
}

void build_headers(struct iovec* iov, char* topic){
    char send_buffer[1024];

    //0: http 200 ok
    char* http_ok = "HTTP/1.0 200 OK\r\n";
    unsigned long slen = strlen(http_ok);
    iov[0].iov_base = calloc(slen,1);
    iov[0].iov_len = slen;
    memcpy(iov[0].iov_base, http_ok, slen);

    //1: Content type
    char* content_type = "Content-Type: text/html\r\n";
    slen = strlen(content_type);
    iov[1].iov_base = calloc(slen,1);
    iov[1].iov_len = slen;
    memcpy(iov[1].iov_base, content_type, slen);

    //2: Content length
    unsigned long tlen = strlen(topic);
    sprintf(send_buffer, "Content-Length: %ld\r\n",tlen);
    slen = strlen(send_buffer);
    iov[2].iov_base = calloc(slen,1);
    iov[2].iov_len = slen;
    memcpy(iov[2].iov_base, send_buffer, slen);

    //3: topic headers
    iov[3].iov_base = calloc(tlen,1);
    iov[3].iov_len = tlen;
    memcpy(iov[3].iov_base, topic, tlen);

    //4: line ending
    char* ending = "\r\n";
    slen = strlen(ending);
    iov[4].iov_base = calloc(slen,1);
    iov[4].iov_len = slen;
    memcpy(iov[4].iov_base, ending, slen);
}

//
void parse_response(){
    /*
    Idea:
    - the publisher would publish new messages at random, each under various topics
    - When publisher starts, it has a "clean slate"
    - subscribers need to register with the publisher by sending a request
        formatted a certain way (e.g. topics to subscribe to)
    - each new registered subscriber gets an id (file descriptor?)
    - add_write_request would only submit entries to the queue for the applicable subscription requests
        (req->client_socket) client_socket would be the applicable subscriber
    - add_write_request would also take a message topic string
        it uses this to determine which subscribers to send the message to
        When a message needs to be sent:
            go through the list of known subscribers and their topic strings
            select only those subscribers whose topic string satisfies a match with the new message
            call io_uring_prep_writev and io_uring_sqe_set_data in a for loop for all the matching subscribers
    - ideally, a mapping would be stored. 
        Create a 2D array. Map a topic string to an array of subscriber ids/fds

    -topic format (header):

    1. {toplevel} -> subscribe to everything under a certain toplevel
    2. {toplevel:child1} -> subscribe only to child1 from toplevel
    3. {toplevel:child1:grandchild1} -> subscribe only to grandchild1
    4. {toplevel:child1,child2} -> subscribe to child1 and child2
    5. {toplevel:child1,child2:grandchild3} -> subscribe to child1 and the 3rd child of child2
    (This is what the subscriber sends to update its subscription)

    (new message to be sent):
    {toplevel:child2}

    entries 1 and 4 get this message
    check for "{toplevel}" and ( "child2}" or "child2," )

    This might require that every topic and subtopic name be unique

    */
}

int build_topic_tree(struct topic_tree** tree){
    int num_children = 0;
    *tree = calloc(sizeof(struct topic_tree),1);
    (*tree)->topic = "toplevel";
    (*tree)->num_children = 3;
    (*tree)->child[0] = malloc(sizeof(struct topic_tree));
    (*tree)->child[0]->topic = "child1";
    (*tree)->child[0]->num_children = 3;
    (*tree)->child[0]->child[0] = malloc(sizeof(struct topic_tree));
    (*tree)->child[0]->child[1] = malloc(sizeof(struct topic_tree));
    (*tree)->child[0]->child[2] = malloc(sizeof(struct topic_tree));
    (*tree)->child[0]->child[0]->topic = "grand1";
    (*tree)->child[0]->child[1]->topic = "grand2";
    (*tree)->child[0]->child[2]->topic = "grand3";

    (*tree)->child[1] = malloc(sizeof(struct topic_tree));
    (*tree)->child[1]->topic = "child2";
    (*tree)->child[0]->num_children = 1;
    (*tree)->child[1]->child[0] = malloc(sizeof(struct topic_tree));
    (*tree)->child[1]->child[0]->topic = "grand4";
    // (*tree)->child[1]->child[0]->num_children = 0;

    (*tree)->child[2] = malloc(sizeof(struct topic_tree));
    (*tree)->child[2]->topic = "child3";
    (*tree)->child[2]->num_children = 2;
    (*tree)->child[2]->child[0] = malloc(sizeof(struct topic_tree));
    (*tree)->child[2]->child[1] = malloc(sizeof(struct topic_tree));
    (*tree)->child[2]->child[0]->topic = "grand5";
    (*tree)->child[2]->child[1]->topic = "grand6";

    num_children = 10;
    return num_children;
}

int main(int argc, char* argv){
    struct topic_tree* topics;
    int num_children = 0;
    num_children = build_topic_tree(&topics);
    topics->num_children = num_children;

    int server_fd;    
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT),
    };
    int opt = 1;

    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT | SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 200) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
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
    subset->socket = server_fd;
    subset->subs = subs;
    signal(SIGINT,sigint_handler);

    //io_uring setup
    struct io_uring_cqe* cqe;
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    //we first need to accept the server's request
    if(add_accept_request(server_fd, (struct sockaddr_in*)&client_addr, &client_addr_len) < 0){
        fprintf(stderr, "add_accept_request failed: %s\n",strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("[PUB] Listening on port %d...\n",PORT);

    //Array mapping subscriber fds to topics
    char subscriber_topics[MAX_SUBS + 5][MAX_TOPIC_LEN] = {0};

    //for generating random topics
    srand(time(NULL));

    int ret = 0;
    while(running){
        /*
        Todo:
        configure headers correctly (as in send_headers())
        */
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
        // struct request* write_req = malloc(sizeof(struct request) + sizeof(struct iovec));
        // unsigned long slen = strlen("RESPONSE");
        // write_req->iovec_count = 1;
        // write_req->client_socket = req->client_socket;
        // write_req->iov[0].iov_base = malloc(slen);
        // write_req->iov[0].iov_len = slen;
        // memcpy(write_req->iov[0].iov_base, "RESPONSE", slen);
        // add_write_request(write_req);

        switch (req->event_type) {
            case EVENT_TYPE_ACCEPT:
                if(add_accept_request(server_fd, &client_addr, &client_addr_len) < 0){
                    fprintf(stderr, "add_accept_request failed: %s\n",strerror(errno));
                    exit(EXIT_FAILURE);
                }
                //we've accepted the request, so now we need to read it
                if(add_read_request(cqe->res) < 0){
                    fprintf(stderr, "add_read_request failed: %s\n",strerror(errno));
                    exit(EXIT_FAILURE);
                }
                //Once we handle the action for a request, we don't need it anymore, so we free it
                free(req);
                break;
            case EVENT_TYPE_READ:
                // io_uring_peek_cqe(&ring,&cqe);
                printf("Request received (socket %d):\n%s\n",req->client_socket,(char*)req->iov[0].iov_base);
                memcpy(subscriber_topics[req->client_socket],(char*)req->iov[0].iov_base,MAX_TOPIC_LEN);
                // printf("subscriber_topics[%d]: %s\n",req->client_socket,subscriber_topics[req->client_socket]);
                //if the client closed the connection, we don't want to add a new write request,
                //since we can't use that socket/fd (it was just closed).
                if (cqe->res == 0) {    //read returns 0 (end of file)
                    printf("Client disconnected. Closing socket %d\n", req->client_socket);
                    close(req->client_socket);
                    free(req->iov[0].iov_base);
                    free(req);
                    break;
                }
                //create new (write) request to add to queue
                //we've received the request, now we're sending the response
                struct request* write_req = malloc(sizeof(struct request) + sizeof(struct iovec) * 5);
                char* headers = calloc(MAX_TOPIC_LEN,1);
                generate_request_data(&headers,NULL,topics);
                write_req->iovec_count = 5;
                write_req->client_socket = req->client_socket;
                build_headers(write_req->iov,headers);
                // memcpy(write_req->iov[0].iov_base, RESPONSE, slen);
                //once we've read the request, we can add a new write request to the queue
                if(add_write_request(write_req) < 0){
                    fprintf(stderr, "add_write_request failed: %s\n",strerror(errno));
                    exit(EXIT_FAILURE);
                }
                // puts("Added request");
                free(req->iov[0].iov_base);
                free(headers);
                free(req);
                break;
            case EVENT_TYPE_WRITE:
                puts("Sending response:");
                for(int i = 0; i < req->iovec_count; i++){
                    printf("%s",(char*)req->iov[i].iov_base);
                }
                //add_write_request(req);
                if(add_read_request(req->client_socket) < 0){
                    fprintf(stderr, "add_read_request failed: %s\n",strerror(errno));
                    exit(EXIT_FAILURE);
                }
                for (int i = 0; i < req->iovec_count; i++) {
                    free(req->iov[i].iov_base);
                }
                free(req);
                break;
            default:
                puts("Other event case");
                break;
        }

        for(int i = 0; i < MAX_SUBS + 5; i++){
            if(strcmp(subscriber_topics[i],"") != 0){
                printf("subscriber_topics[%d]:\n%s\n",i,subscriber_topics[i]);
            }
        }
       
        //Mark this request as processed
        io_uring_cqe_seen(&ring, cqe);
    }

    free(subset);
    return 0;
}