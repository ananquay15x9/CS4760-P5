// Author: Tu Le
//CS4760 Project 5


#include "shared1.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#define MAX_PROCESSES 40
#define LOG_LINE_LIMIT 10000

// Globals
int msqid;
int shmid;
struct sim_clock* simClock;
FILE* logfile;
int log_line_count = 0;
int totalProcesses = 0;
struct process_table_entry processTable[18];

void oss_log(const char* fmt, ...) {
    if (log_line_count >= LOG_LINE_LIMIT) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    if (logfile) vfprintf(logfile, fmt, args);
    va_end(args);
    log_line_count++;
}

void cleanup_shared_memory() {
    if (simClock) {
        shmdt(simClock);
    }
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msqid, IPC_RMID, NULL);
}

void sigint_handler(int sig) {
    oss_log("OSS: Caught SIGINT, cleaning up...\n");
    for (int i = 0; i < 18; i++) {
        if (processTable[i].pid != 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }
    while (wait(NULL) > 0);
    cleanup_shared_memory();
    if (logfile) fclose(logfile);
    exit(1);
}

void printResourceTable() {
	oss_log("Current system resources:\n");
	for (int i = 0; i < 5; i++) {
		oss_log("R%d available\n", i);
	}
}

void printProcessTable() {
	oss_log("Current process table:\n");
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid != 0) 
			oss_log("Slot %d: PID %d\n", i, processTable[i].pid);
	}
}


int main(int argc, char* argv[]) {
    signal(SIGINT, sigint_handler);

    char* log_filename = "oss1.log";
    int verbose = 0;
    int opt;

    while ((opt = getopt(argc, argv, "vf:")) != -1) {
        switch (opt) {
	    case 'v':
	        verbose = 1;
		break;
	    case 'f':
		log_filename = optarg;
		break;
	    default:
		fprintf(stderr, "Usage: %s [-v] [-f logfile]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
    }	
	
    logfile = fopen(log_filename, "w");
    key_t key = ftok("oss", 1);

    shmid = shmget(key, sizeof(struct sim_clock), 0666 | IPC_CREAT);
    simClock = (struct sim_clock*)shmat(shmid, NULL, 0);
    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    msqid = msgget(key, 0666 | IPC_CREAT);

    srand(time(NULL));

    memset(processTable, 0, sizeof(processTable));
    int event_count = 0;

    while (1) {
        // a. Clean up any dead children
        pid_t childPid;
        int status;
        while ((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < 18; i++) {
                if (processTable[i].pid == childPid) {
                    processTable[i].pid = 0;
                    break;
                }
            }
        }

        // b. Count running children
        int runningChildren = 0;
        for (int i = 0; i < 18; i++) {
            if (processTable[i].pid != 0) runningChildren++;
        }

        // c. Fork if possible
        if (runningChildren < 18 && totalProcesses < MAX_PROCESSES) {
            pid_t pid = fork();
            if (pid == 0) {
                char bound_B_str[20];
                sprintf(bound_B_str, "%d", 100000);
                execl("./user_proc", "user_proc", bound_B_str, NULL);
                perror("execl");
                exit(1);
            } else if (pid > 0) {
                for (int i = 0; i < 18; i++) {
                    if (processTable[i].pid == 0) {
                        processTable[i].pid = pid;
                        break;
                    }
                }
                oss_log("OSS: Launched child process %d\n", pid);
                totalProcesses++;
            }
        }

        // d. Simulate clock advance
        simClock->nanoseconds += (rand() % 1000) + 1;
        if (simClock->nanoseconds >= 1000000000) {
            simClock->seconds++;
            simClock->nanoseconds -= 1000000000;
        }

	if (verbose && event_count % 20 == 0) {
	    printResourceTable();
	    printProcessTable();
	}

	event_count++;

        // e. Termination condition
        if ((totalProcesses >= MAX_PROCESSES && runningChildren == 0) || simClock->seconds >= 5) {
            oss_log("OSS: Terminating at time %u:%u\n", simClock->seconds, simClock->nanoseconds);
            break;
        }
    }

    for (int i = 0; i < 18; i++) {
        if (processTable[i].pid != 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }
    while (wait(NULL) > 0);

    cleanup_shared_memory();
    if (logfile) fclose(logfile);
    return 0;
}

