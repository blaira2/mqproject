#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

// #define DEFAULT_PORT 5555
#define MICROSERVICE_PORT 4444

int main(int argc, char* argv[]){
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
    int sub_pid = fork();
    if(sub_pid < 0){
        fprintf(stderr,"Fork for subscriber failed: %s\n",strerror(errno));
        exit(EXIT_FAILURE);
    }
    char* topic = "test";
    if(argc > 1){
        topic = argv[1];
    }
    if(sub_pid == 0){
        char* args[] = {
            "subscriber",
            "127.0.0.1",
            topic,
            NULL
        };
        // puts("Going to start subscriber");
        execv("./subscriber",args);
    }
    // puts("After forks");
    // sleep(1);

    //act as a server (send and maybe receive messages)

    struct sockaddr_in pub_addr, client;
    socklen_t addr_size = sizeof(pub_addr);
    pub_addr.sin_family = AF_INET;
    pub_addr.sin_addr.s_addr = INADDR_ANY;
    pub_addr.sin_port = htons(MICROSERVICE_PORT);

    int pub_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(pub_socket < 0){
        fprintf(stderr,"Error on socket: %s\n",strerror(errno));
        exit(EXIT_FAILURE);
    }
    // puts("here1");
    if(bind(pub_socket, (struct sockaddr*) &pub_addr, addr_size) < 0){
        fprintf(stderr,"Failed to bind: %s\n",strerror(errno));
        exit(EXIT_FAILURE);
    }
    // puts("here2");

    if(listen(pub_socket, 200) < 0){
        fprintf(stderr,"Failed to listen: %s\n",strerror(errno));
        exit(EXIT_FAILURE);
    }
    int len = sizeof(client);
    // puts("About to accept");
    int conn_fd = accept(pub_socket, (struct sockaddr*) &client, &len);
    if(conn_fd < 0){
        fprintf(stderr, "microservice server failed to accept: %s\n",strerror(errno));
        exit(EXIT_FAILURE);
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
    while(1){
        memset(to_send, 0, sizeof(to_send));
        // printf("Microservice command> ");
        if(!fgets(to_send, sizeof(to_send), stdin)){
            fprintf(stderr, "Error: fgets failed. %s.\n", strerror(errno));
            close(pub_socket);
            return EXIT_FAILURE;
        }
        printf("Microservice to send: %s\n",to_send);
        if(send(conn_fd,to_send,strlen(to_send),0) < 0){
            fprintf(stderr, "Error: Failed to send data. %s.\n", strerror(errno));
            close(pub_socket);
            return EXIT_FAILURE;
        }
        // puts("End of loop");
    }


    // char buff[1024];
    // int n;
    // while(1){
    //     read(conn_fd)
    // }

    close(pub_socket);
    puts("Microservice done");
}