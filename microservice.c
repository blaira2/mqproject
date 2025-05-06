#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char* argv[]){
    int pub_pid = fork();
    if(pub_pid < 0){
        exit(EXIT_FAILURE);
    }
    //fork and exec to spawn publisher
    if(pub_pid == 0){
        execl("./publisher","publisher",NULL);
    }
    int sub_pid = fork();
    if(sub_pid < 0){
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
        execv("./subscriber",args);
    }
}