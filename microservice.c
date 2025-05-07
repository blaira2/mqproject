#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

// #define DEFAULT_PORT 5555
#define MICROSERVICE_PORT 4444
#define MAX_TOPIC_LEN 64
#define MAX_BUFFER_SIZE 1024

int main(int argc, char* argv[]){
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
        execl("./publisher","publisher",NULL);
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

    
    // if(argc > 1){
    //     topic = argv[1];
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
    pub_addr.sin_port = htons(MICROSERVICE_PORT);

    int pub_socket = socket(AF_INET, SOCK_STREAM, 0);
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
    while(1){
        int iterations = (rand() % 500) + 10;
        for(int i = 0; i < iterations; i++){
            memset(message, 0, MAX_BUFFER_SIZE + MAX_TOPIC_LEN);
            num = rand() % topic_count;
            strncpy(message, topics[num], MAX_TOPIC_LEN);
            strcat(message, " ");
            num = rand() % topic_count;
            strncat(message, messages[num], MAX_TOPIC_LEN);
            strcat(message, " new message\0");
            // printf("Microservice to send: %s\n",message);
            if(send(conn_fd,message,strlen(message),0) < 0){
                fprintf(stderr, "Error: Failed to send data. %s.\n", strerror(errno));
                goto EXIT;
            }
            usleep(500);
        }
        sleep(1);
    }

    // while(1){
    //     memset(to_send, 0, sizeof(to_send));
    //     // printf("Microservice command> ");
    //     if(!fgets(to_send, sizeof(to_send), stdin)){
    //         fprintf(stderr, "Error: fgets failed. %s.\n", strerror(errno));
    //         goto EXIT;
    //     }
    //     printf("Microservice to send: %s\n",to_send);
    //     if(send(conn_fd,to_send,strlen(to_send),0) < 0){
    //         fprintf(stderr, "Error: Failed to send data. %s.\n", strerror(errno));
    //         goto EXIT;
    //     }
    //     // puts("End of loop");
    // }


    // char buff[1024];
    // int n;
    // while(1){
    //     read(conn_fd)
    // }

EXIT:
    free(topic);
    close(pub_socket);
    puts("Microservice done");
}