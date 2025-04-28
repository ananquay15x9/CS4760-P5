//Author: Tu Le
//CS4760

#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <time.h>

int main(int argc, char* argv[]) {
    key_t key = ftok("oss", 1);
    int msqid = msgget(key, 0666);
    srand(getpid());

    while (1) {
        usleep(rand() % 1000);

        struct oss_message request;
        request.mtype = getpid();
        request.command = (rand() % 2) ? 1 : 2;
        request.resourceId = rand() % 5;

        if (msgsnd(msqid, &request, sizeof(request) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(1);
        }

        struct worker_message response;
        if (msgrcv(msqid, &response, sizeof(response) - sizeof(long), getpid(), 0) == -1) {
            perror("msgrcv");
            exit(1);
        }

        if (rand() % 5 == 0) { // Randomly terminate
            request.command = 3;
            request.mtype = getpid();
            if (msgsnd(msqid, &request, sizeof(request) - sizeof(long), 0) == -1) {
                perror("msgsnd terminate");
            }
            if (msgrcv(msqid, &response, sizeof(response) - sizeof(long), getpid(), 0) == -1) {
                perror("msgrcv terminate");
            }
            break;
        }
    }

    return 0;
}


