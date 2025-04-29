//Author: Tu Le
//CS4760 Project 5
//Data: 4/25/2025

#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/msg.h> //for message queues
#include <time.h> //for generating random numbers
#include <signal.h>

//Constants 
#define MSGKEY 12345
#define MAX_RESOURCES 5
#define MAX_INSTANCES 10
#define REQUEST_RESOURCE 1
#define RELEASE_RESOURCE 2
#define TERMINATE 3 // add termiante command


SimulatedClock *simClock;
int shmid;
int msqid;

void handle_sigterm(int sig) {
	printf("Process %d received SIGTERM, exiting.\n", getpid());
	exit(0);
}

int attach_shared_memory() {
	key_t key = ftok("oss.c", 1); //Same key as in oss.c
	if (key == -1) {
		perror("ftok");
		return 1;
	}
	
	shmid = shmget(key, sizeof(SimulatedClock), 0666); //No IPC_CREAT
	if (shmid == -1) {
		perror("shmget");
		return 1;
	}

	simClock = (SimulatedClock *)shmat(shmid, NULL, 0);
	if (simClock == (SimulatedClock *) -1) {
		perror("shmat");
		return 1;
	}
	return 0;
}

void detach_shared_memory() {
	if (simClock != (SimulatedClock *)-1) {
		shmdt(simClock);
	}
}

int main(int argc, char *argv[]) {
	signal(SIGTERM, handle_sigterm);
	
	//Print struct sizes for debugging
	printf("sizeof(struct oss_message) = %zu\n", sizeof(struct oss_message));
	printf("sizeof(struct worker_message) = %zu\n", sizeof(struct worker_message));

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <bound_B>\n", argv[0]);
		return 1;
	}

	int bound_B = atoi(argv[1]);
	if (bound_B <= 0) {
		fprintf(stderr, "Error: bound_B must be greater than 0\n");
		return 1;
	}

	//Seed the random number generator
	srand(getpid());

	//1. Attach to shared memory
	key_t key =ftok("oss.c", 1);
	if (key == -1) {
		perror("ftok");
		return 1;
	}

	shmid = shmget(key, sizeof(SimulatedClock), 0);
	if (shmid == -1) {
		perror("shmget");
		return 1;
	}

	simClock = (SimulatedClock *)shmat(shmid, NULL, 0);
	if (simClock == (SimulatedClock *) -1) {
		perror("shmat");
		return 1;
	}

	//2. Attach to the message queue
	msqid = msgget(MSGKEY, 0); //No IPC_CREAT, just attach
	if (msqid == -1) {
		perror("msgget");
		shmdt(simClock);
		return 1;
	}

	//3. Initialize resource tracking for this process
	int myResources[MAX_RESOURCES]; //track how many of each resource this process has
	for (int i = 0; i < MAX_RESOURCES; i++) {
		myResources[i] = 0;
	}

	//4. Main loop
	unsigned int last_termination_check_sec = simClock->seconds;
	unsigned int last_termination_check_ns = simClock->nanoseconds;

	while (1) {
		//a. Sleep for a randome time within bound B (simulate work)
		int sleepTime = rand() % bound_B;
		usleep(sleepTime); //sleep in microseconds

		//b. Randomly decide to request or release a resource
		int action = rand() % 2; // 0 for request, 1 for release
		int resourceId = rand() % MAX_RESOURCES;
		struct oss_message oss_msg = {0}; //clear struct memory
		oss_msg.mtype = getpid(); //send message to oss
		oss_msg.resourceId = resourceId;

		if (action == 0) {
			//request a resource
			if (myResources[resourceId] < MAX_INSTANCES) { //can only request if less than max instances
				struct oss_message oss_msg = {0}; //zero out
				oss_msg.mtype = getpid();
				oss_msg.command = REQUEST_RESOURCE;
				oss_msg.resourceId = resourceId;
				printf("Process %d requesting resource %d at time %u:%u\n",
					getpid(), resourceId, simClock->seconds, simClock->nanoseconds);
				printf("USER_PROC sending: mtype=%ld, command=%d, resourceId=%d\n", oss_msg.mtype, oss_msg.command, oss_msg.resourceId); //debug print
				if (msgsnd(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0) == -1) {
					perror("msgsnd (request)");
					break;
				}

				//Wait for a response from oss (for resource requests)
				struct worker_message worker_response;
				if (msgrcv(msqid, &worker_response, sizeof(worker_response) - sizeof(long), getpid(), 0) == -1) {
					perror("msgrcv (response))");
				} else {
					if (worker_response.status == 1) {
						printf("USER_PROC received resource granted or termination confirmation.\n");
					} else {
						printf("USER_PROC received resource denied.\n");
					}
				}
				printf("USER_PROC received: mtype=%ld, status=%d\n", worker_response.mtype, worker_response.status);
				if (worker_response.status == 1) {
					printf("Process %d granted resource %d\n", getpid(), resourceId);
					myResources[resourceId]++; //increment resource count
				} else {
					printf("Process %d denied resource %d\n", getpid(), resourceId);
					//Optionally handle denial (e.g., wait and retry, request a different resource)
				}
			}
		} else {
			//Release a resource
			if (myResources[resourceId] > 0) {
				struct oss_message oss_msg = {0}; //zero out
				oss_msg.mtype = getpid();
				oss_msg.command = RELEASE_RESOURCE;
				oss_msg.resourceId = resourceId;
				printf("Process %d releasing resource %d at time %u:%u\n",
					getpid(), resourceId, simClock->seconds, simClock->nanoseconds);
				if (msgsnd(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0) == -1) {
					perror("msgsnd (release)");
					break;
				}
				myResources[resourceId]--;
			}
		}

		//c. Check if it should terminate (every 250ms)
		unsigned int elapsed_sec = simClock->seconds - last_termination_check_sec;
		unsigned int elapsed_ns = (simClock->nanoseconds >= last_termination_check_ns)
			? (simClock->nanoseconds - last_termination_check_ns)
			: (1000000000 - last_termination_check_ns + simClock->nanoseconds);
		if (elapsed_sec > 0 || elapsed_ns >= 250000000) {
			last_termination_check_sec = simClock->seconds;
			last_termination_check_ns = simClock->nanoseconds;
			if (simClock->seconds > 1 && ((double)rand() / RAND_MAX) < 0.15) {
				//send message to oss to terminate
				struct oss_message oss_msg = {0}; //zero out
				oss_msg.command = TERMINATE;
				oss_msg.resourceId = 0; //clear resourceId
				oss_msg.mtype = getpid();
				if (msgsnd(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0) == -1) {
					perror("msgsnd (terminate)");
				} else {
					struct worker_message worker_response;
					if (msgrcv(msqid, &worker_response, sizeof(worker_response) - sizeof(long), getpid(), 0) == -1) {
						perror("msgrcv (terminate response)");
					} else {
						printf("Process %d received terminate confirmation from OSS\n", getpid());
					}
				}
				printf("Process %d terminating\n", getpid());
				break;
			}
		}
	}

	//Detach from shared memory and message queue
	shmdt(simClock);
	return 0;
}


