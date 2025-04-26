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

//Message structures
struct oss_message {
	long mtype; //PID of the destination process (oss in this case)
	int command; //Type of request 
	int resourceID; //The ID of the resource being requested or released
};

struct worker_message {
	long mtype; //PID of the destination process (user_proc in this case)
	int status; //Status of the request (eg., Granted, DENIED)
};

//Constants 
#define MSGKEY 12345
#define MAX_RESOURCE 5
#define MAX_INSTANCES 10
#define REQUEST_RESOURCE 1
#define RELEASE _RESOURCE 2
#define TERMINATE 3 // add termiante command


SimulatedClock *simClock;
int shmid;
int msqid;

int attach_shared_memory() {
	key_t key = ftok("oss.c", 1); //Same key as in oss.c
	if (key == -1) {
		perror("ftok");
		return 1;
	}
	
	int shmid = shmget(key, sizeof(SimulatedClock), 0666); //No IPC_CREAT
	if (shmid == -1) {
		perror("shmget");
		return 1;
	}

	simClock = (SimulatedClock *)shamt(shmid, NULL, 0);
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
	msquid = msgget(MSGKEY, 0); //No IPC_CREAT, just attach
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
	while (1) {
		//a. Get current time
		unsigned int currentSeconds = simClock->seconds;
		unsigned int currentNanoseconds = simClock->nanoseconds;

		//b. Generate random time within bound B
		int sleepTime = rand() % bound_B;
		usleep(sleepTime); //sleep

		//c. Randomly decide to request or release a resource
		int action = rand() % 2; // 0 for request, 1 for release
		int resourceID;
		
		stuct oss_message oss_msg;
		oss_msg.mtype = getppid(); //send message to oss
		oss_msg.resourceID = rand() % MAX_RESOURCES; //Choose a random resource

		if (action == 0) {
			//request a resource
			resourceId = oss_msg.resourceId;
			if (myResources[resourceId] < MAX_INSTANCES) { //can only request if less than max instances
				oss_msg.command = REQUEST_RESOURCE;
				printf("Process %d requesting resource %d at time %u:%u\n",
					getpid(), oss_msg.resourceId, currentSeconds, currentNanoseconds);
				if (msgsnd(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0) == -1) {
					perror("msgsnd (request)");
					//consider what to do on error: exit, retry, etc. For now, exit
					shmdt(simClock);
					exit(1);
				}

				// d. Wait for a response from oss (for resource requests)
				struct worker_message worker_response;
				if (msgrcv(msqid, &worker_response, sizeof(worker_response) - sizeof(long), getpid(), 0) == -1) {
					perror("msgrcv (response)");
					shmdt(simClock);
					exit(1);
				}

				if (worker_response.status == 0) {
					printf("Process %d granted resource %d\n", getpid(), oss_msg.resourceId);
					myResources[oss_msg.resourceId]++; //increment resource count
				} else {
					printf("Process %d denied resource %d\n", getpid(), oss_msg.resourceId);
					//Optionally handle denial (e.g., wait and retry, request a different resource)
				}
			}
		} else {
			//Release a resource
			resourceId = oss_msg.resourceId;
			if (myResources[resourceId] > 0) {
				oss_msg.command = RELEASE_RESOURCE;
				printf("Process %d releasing resource %d at time %u:%u\n",
					getpid(), oss_msg.resourceId, currentSeconds, currentNanoseconds);
				if (msgsnd(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0) == -1 {
					perror("msgsnd (release)");
					shmdt(simClock);
					exit(1);
				}
				myResources[oss_msg.resourceId]--;
			}

			//e. Check if it should terminate (every 250ms)
			if ((currentNanoseconds % 250000000) == 0) { //check every 250ms
				if (currentSeconds > 1 && ((double)rand() / RAND_MAX) < 0.15) {
					//send message to oss to terminate
					oss_msg.command = TERMINATE;
					oss_msg.mtype = getppid();
					if (msgsnd(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0) == -1) {
						perror("msgsnd (terminate)");
						shmdt(simClock);
						exit(1);
					}
					printf("Process %d terminating\n", getpid());
					break; //Exit the main loop
				}
			}

			//f. Update simulated clock (simplified for this example)
			simClock->nanoseconds += 1000000; //Increment by 1ms
			if (simClock->nanoseconds >= 1000000000) {	
				simClock->seconds++;
				simClock->nanoseconds -= 1000000000;
			}
		}
		
		//5. Detach from shared memory and message queue
		shmdt(simClock);
		return 0;
}
			
	printf("User proc started, clock: %u:%u\n", simClock->seconds, simClock->nanoseconds);

	detach_shared_memory();
	return 0;
}
