// publisher_zmq.c
#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>

#define ENDPOINT "tcp://*:5556"
#define MAX_LINE 1024

#define MICROSERVICE_PORT 4444

char* microservice_message;
int microservice_fd = -1;
int pipe_fds[2];    //used to write data from microservice thread to sending thread

void* microservice_listener_thread(void* arg){
    //act as client - receive message from server

    printf("microservice listener started\n");
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "Thread failed to create socket: %s\n",strerror(errno));
    }
    struct sockaddr_in ms_addr;
    ms_addr.sin_family = AF_INET;
    ms_addr.sin_port = htons(MICROSERVICE_PORT);
    ms_addr.sin_addr.s_addr = INADDR_ANY;
    int addrlen = sizeof(ms_addr);

    int conn = connect(sockfd, (struct sockaddr*) &ms_addr, sizeof(ms_addr));
    char inbuf[1024];
    if(conn < 0){
        fprintf(stderr, "Thread failed to connect: %s\n",strerror(errno));
    }
    else {
        if(pipe(pipe_fds) < 0){
            fprintf(stderr,"Pipe failed: %s\n",strerror(errno));
            exit(1);
        }
        while(1){
            int recvbytes = recv(sockfd, microservice_message, sizeof(inbuf), 0);
            if(recvbytes < 0){
                fprintf(stderr, "Thread failed to receive: %s\n",strerror(errno));
            }
            if(write(pipe_fds[1],microservice_message,strlen(microservice_message)) < 0){
                fprintf(stderr, "Thread failed to write to ms fd: %s\n",strerror(errno));
            }
            memset(microservice_message, 0, 1024);
        }
    }
    puts("microservice exit");
    pthread_exit(NULL);
}


int main() {
    void *ctx = zmq_ctx_new();
    void *pub = zmq_socket(ctx, ZMQ_PUB);
    if (zmq_bind(pub, ENDPOINT) != 0) {
        perror("zmq_bind");
        return 1;
    }
    printf("ZeroMQ PUB bound at %s\n", ENDPOINT);

    pthread_t microservice_thread;
    microservice_message = calloc(1024,1);
    if (pthread_create(&microservice_thread, NULL, microservice_listener_thread, &microservice_fd) != 0) {
        perror("pthread_create");
        free(microservice_message);
        return 1;
    }
    puts("zmq aboit to loop");
    //publisher loop
    while (1) {
        char line[MAX_LINE];
        memset(line, 0, sizeof(line));
        if(pipe_fds[0] == STDIN_FILENO){
            //try again to ensure that only messages from microservice are received
            puts("pipe fd 0");
            if(pipe(pipe_fds) < 0){
                fprintf(stderr,"Pipe failed: %s\n",strerror(errno));
                exit(1);
            }
            sleep(5);
            continue;
        }
        if(read(pipe_fds[0],line,sizeof(line)) < 0){
            fprintf(stderr, "Could not read from ms fd: %s\n",strerror(errno));
            continue;
        }
        // printf("Read data: %s\n",line);
        char *topic = strtok(line, " ");
        char *msg   = strtok(NULL, "\n");
        if (!topic || !msg) {
            fprintf(stderr, "Usage: <topic> <message>\n");
            continue;
        }
        // send topic frame
        if (zmq_send(pub, topic, strlen(topic), ZMQ_SNDMORE) < 0) {
            perror("zmq_send topic");
            break;
        }
        // send payload frame
        if (zmq_send(pub, msg, strlen(msg), 0) < 0) {
            perror("zmq_send msg");
            break;
        }
    }

    // while (fgets(line, sizeof(line), stdin)) {
    //     // Expect input: "<topic> <message>\n"
    //     char *topic = strtok(line, " ");
    //     char *msg   = strtok(NULL, "\n");
    //     if (!topic || !msg) {
    //         fprintf(stderr, "Usage: <topic> <message>\n");
    //         continue;
    //     }
    //     // send topic frame
    //     if (zmq_send(pub, topic, strlen(topic), ZMQ_SNDMORE) < 0) {
    //         perror("zmq_send topic");
    //         break;
    //     }
    //     // send payload frame
    //     if (zmq_send(pub, msg, strlen(msg), 0) < 0) {
    //         perror("zmq_send msg");
    //         break;
    //     }
    // }

    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    return 0;
}
