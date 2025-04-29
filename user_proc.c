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
#include <stdbool.h> //bool type
#include <string.h>

//Constants 
#define MSGKEY 12345

//Constants for message commands - match oss.c
#define REQUEST_RESOURCE 1
#define RELEASE_RESOURCE 2
#define TERMINATE 3

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

// Function to safely send a message and wait for response
bool send_message(int command, int resourceId) {
	struct oss_message msg;
	memset(&msg, 0, sizeof(struct oss_message));
	msg.mtype = getpid();
	msg.command = command;
	msg.resourceId = resourceId;

	if (msgsnd(msqid, &msg, sizeof(struct oss_message) - sizeof(long), 0) == -1) {
		return false;
	}

	struct worker_message response;
	memset(&response, 0, sizeof(struct worker_message));
	if (msgrcv(msqid, &response, sizeof(struct worker_message) - sizeof(long), getpid(), 0) == -1) {
		return false;
	}

	return response.status == 1;
int main(int argc, char *argv[]) {
	signal(SIGTERM, handle_sigterm);
	
	
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <bound_B>\n", argv[0]);
		return 1;
	}

	int bound_B = atoi(argv[1]);
	if (bound_B <= 0) {
		fprintf(stderr, "Error: bound_B must be greater than 0\n");
		return 1;
	}

	srand(getpid() + time (NULL)); //Better randomization

	//Attach to shared memory and message queue
	if (attach_shared_memory() != 0) {
		return 1;
	}

	msqid = msgget(MSGKEY, 0);
	if (msqid == -1) {
		perror("msgget");
		detach_shared_memory();
		return 1;
	}

	//Track resources and start time
	int myResources[NUM_RESOURCES] = {0};
	unsigned int start_sec = simClock->seconds;
	unsigned int start_ns = simClock->nanoseconds;
	unsigned int last_check_sec = start_sec;
	unsigned int last_check_ns = start_ns;
	int total_requests = 0; //track total request made
	int consecutive_denials = 0;

	//Main process loop
	while (1) {
		//Sleep for a shorter time to be more active
		usleep(rand() % bound_B);

		// Get current time
		unsigned int curr_sec = simClock->seconds;
		unsigned int curr_ns = simClock->nanoseconds;
		
		bool can_terminate = (curr_sec > start_sec + 1) || 
						   (curr_sec == start_sec + 1 && curr_ns >= start_ns);

		//Count total resources held
		int total_held = 0;
		for (int i = 0; i < NUM_RESOURCES; i++) {
			total_held += myResources[i];
		}

		//Check for temination every 250ms
		unsigned int elapsed_ns = (curr_sec - last_check_sec) * 1000000000 + 
								(curr_ns - last_check_ns);
		
		if (elapsed_ns >= 250000000) {  // 250ms in nanoseconds
			last_check_sec = curr_sec;
			last_check_ns = curr_ns;
			
			
			
			// Consider termination if we've made enough requests
			if (can_terminate && (total_requests >= 5 || consecutive_denials >= 3)) {
				// Release all resources first
				for (int i = 0; i < NUM_RESOURCES; i++) {
					while (myResources[i] > 0) {
						if (send_message(RELEASE_RESOURCE, i)) {
							myResources[i]--;
						}
					}
				}		
				// Then send terminate message
				if (send_message(TERMINATE, 0)) {
					break;
				}
			}
		}

		// Request or release resources
		  if (total_held < 3 && total_requests < 15 && consecutive_denials < 3) {
			// Try to request a resource
			int resourceId = rand() % NUM_RESOURCES;
			if (myResources[resourceId] < 2) {  // Maximum 2 of any resource
				if (send_message(REQUEST_RESOURCE, resourceId)) {
					myResources[resourceId]++;
					consecutive_denials = 0;
					total_requests++;
				} else {
					consecutive_denials++;
				}
			}
		} else if (total_held > 0) {
			// Release a random held resource
			int resourceId = rand() % NUM_RESOURCES;
			if (myResources[resourceId] > 0) {
				if (send_message(RELEASE_RESOURCE, resourceId)) {
					myResources[resourceId]--;
					consecutive_denials = 0;
				}
			}
		}
	}

	detach_shared_memory();
	return 0;
}


