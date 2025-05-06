#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
    if(sub_pid == 0){
        char* args[] = {
            "subscriber",
            "127.0.0.1",
            "5555",
            "test",
            NULL
        };
        execv("./subscriber",args);
    }
}