#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>

#define DEFAULT_PORT 4444
#define ZMQ_PORT 5556
#define MAX_TOPIC_LEN 64
#define MAX_BUFFER_SIZE 1024
#define DEFAULT_ADDR "127.0.0.1"
#define ZMQ_ADDR "tcp://127.0.0.1:5556"

#define REQ_COUNT 1000000

double getdetlatimeofday(struct timeval *begin, struct timeval *end) {
    return (end->tv_sec + end->tv_usec * 1.0 / 1000000) -
           (begin->tv_sec + begin->tv_usec * 1.0 / 1000000);
}

int main(int argc, char* argv[]){
    char* usage = "Usage: %s <\"reg\"|\"zmq\">\n";
    if(argc != 2){
        printf(usage,argv[0]);
        return 1;
    }

    struct timeval begin, end;

    char* pub_prog, *endpoint;
    int port = DEFAULT_PORT;
    if(strcmp(argv[1], "reg") == 0){
        pub_prog = "./publisher";
        endpoint = DEFAULT_ADDR;
    }
    else if(strcmp(argv[1], "zmq") == 0){
        pub_prog = "./zmq_publisher";
        endpoint = ZMQ_ADDR;
        // port = ZMQ_PORT;
    }
    else {
        printf(usage,argv[0]);
        return 1;
    }

    //don't fail if a subscriber drops out
    signal(SIGPIPE, SIG_IGN);

    char* topics[] = {
        "DoctorInfo",
        "PatientResults",
        "TestData",
        "StaffingData",
    };
    char* messages[] = { 
        "Doctor info received",
        "Patient results available",
        "Test data needs to be processed",
        "Staff need to be hired",
    };
    srand(time(NULL));

    int pub_pid = fork();
    // puts("Just forked once");
    if(pub_pid < 0){
        fprintf(stderr,"Fork for publisher failed: %s\n",strerror(errno));
        exit(EXIT_FAILURE);
    }
    //fork and exec to spawn publisher
    if(pub_pid == 0){
        // puts("Going to start publisher");
        execl(pub_prog,"zmq_publisher",NULL);
    }
    else {
        // puts("parent");
    }
    // puts("publisher started");
    // sleep(1);
    int topic_count = sizeof(topics)/sizeof(topics[0]);
    int num = rand() % topic_count;
    char* topic = calloc(MAX_TOPIC_LEN, 1);
    strncpy(topic, topics[num], MAX_TOPIC_LEN);

    // int sub_pid = fork();
    // if(sub_pid < 0){
    //     fprintf(stderr,"Fork for subscriber failed: %s\n",strerror(errno));
    //     exit(EXIT_FAILURE);
    // }

    // if(sub_pid == 0){
    //     char* args[] = {
    //         "subscriber",
    //         "127.0.0.1",
    //         topic,
    //         NULL
    //     };
    //     // puts("Going to start subscriber");
    //     execv("./subscriber",args);
    // }

    //act as a server (send and maybe receive messages)

    struct sockaddr_in pub_addr, client;
    socklen_t addr_size = sizeof(pub_addr);
    pub_addr.sin_family = AF_INET;
    pub_addr.sin_addr.s_addr = INADDR_ANY;
    pub_addr.sin_port = htons(port);

    int pub_socket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(pub_socket, SOL_SOCKET, MSG_NOSIGNAL, &opt, sizeof(int));
    if(pub_socket < 0){
        fprintf(stderr,"Error on socket: %s\n",strerror(errno));
        goto EXIT;
    }
    // puts("here1");
    if(bind(pub_socket, (struct sockaddr*) &pub_addr, addr_size) < 0){
        fprintf(stderr,"Failed to bind: %s\n",strerror(errno));
        goto EXIT;
    }
    // puts("here2");

    if(listen(pub_socket, 200) < 0){
        fprintf(stderr,"Failed to listen: %s\n",strerror(errno));
        goto EXIT;
    }
    int len = sizeof(client);
    // puts("About to accept");
    int conn_fd = accept(pub_socket, (struct sockaddr*) &client, &len);
    setsockopt(conn_fd, SOL_SOCKET, MSG_NOSIGNAL, &opt, sizeof(int));

    if(conn_fd < 0){
        fprintf(stderr, "microservice server failed to accept: %s\n",strerror(errno));
        goto EXIT;
    }
    // puts("here3");

    // printf("connfd: %d\n",conn_fd);

    // if(connect(pub_socket, (struct sockaddr*) &pub_addr, addr_size) < 0){
    //     fprintf(stderr, "Error: Failed to create socket. %s.\n", strerror(errno));
    //     return EXIT_FAILURE;
    // }
    int pub_status, sub_status;
    char to_send[1024] = {0};
    // puts("Parent loop");
    char* message = calloc(MAX_BUFFER_SIZE + MAX_TOPIC_LEN, 1);
    // strncpy(message, topic, MAX_TOPIC_LEN);
    char* requests[REQ_COUNT];
    for(int i = 0; i < REQ_COUNT; i++){
        requests[i] = calloc(MAX_BUFFER_SIZE, 1);
        num = rand() % topic_count;
        strncpy(requests[i], topics[num], MAX_TOPIC_LEN);
        strcat(requests[i], " ");
        num = rand() % topic_count;
        strncat(requests[i], messages[num], MAX_TOPIC_LEN);
        strcat(requests[i], " new message\0");
    }
    puts("Done crafting requests");
    while(1){
START_WHILE:
        // int iterations = (rand() % 500) + 10;
        gettimeofday(&begin, NULL);
        int dropped = 0;
        for(int i = 0; i < REQ_COUNT; i++){
            if(send(conn_fd,requests[i],strlen(requests[i]),MSG_NOSIGNAL) < 0){
                dropped = 1;
                fprintf(stderr, "Error: Failed to send data. %s.\n", strerror(errno));
                goto START_WHILE;
                // goto EXIT;
                // continue;
            }
            //some buffering(?) causes requests to not be individual. This fixes(?) that
            // usleep(100);
        }
        gettimeofday(&end, NULL);
        double delta = getdetlatimeofday(&begin, &end);
        if(send(conn_fd,"stat stat",strlen("stat stat"),MSG_NOSIGNAL) < 0){
            dropped = 1;
            fprintf(stderr, "Error: Failed to send data. %s.\n", strerror(errno));
            goto START_WHILE;
            // goto EXIT;
            // continue;
        }
        printf("Finished sending %d requests in %.10f seconds\n",REQ_COUNT,delta);
        // sleep(1);
    }

EXIT:
    for(int i = 0; i < REQ_COUNT; i++){
        free(requests[i]);
    }
    free(topic);
    close(pub_socket);
    puts("Microservice done");
}