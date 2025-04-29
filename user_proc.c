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
		shmdt(simClock);
		return 1;
	}

	//Track resources and start time
	int myResources[NUM_RESOURCES] = {0};
	unsigned int start_sec = simClock->seconds;
	unsigned int start_ns = simClock->nanoseconds;
	unsigned int last_check_sec = start_sec;
	unsigned int last_check_ns = start_ns;
	int total_requests = 0; //track total request made

	//Main process loop
	while (1) {
		//Sleep for a shorter time to be more active
		usleep(rand() % (bound_B / 3));

		//b. Randomly decide to request or release a resource
		int action = rand() % 2; // 0 for request, 1 for release

		// Get current time
		unsigned int curr_sec = simClock->seconds;
		unsigned int curr_ns = simClock->nanoseconds;
		
		bool can_terminate = (curr_sec > start_sec + 1) || 
						   (curr_sec == start_sec + 1 && curr_ns >= start_ns);

		// Check if we have any resources
		bool has_resources = false;
		for (int i = 0; i < NUM_RESOURCES; i++) {
			if (myResources[i] > 0) {
				has_resources = true;
				break;
			}
		}

		//Check for temination every 250ms
		unsigned int elapsed_ns = (curr_sec - last_check_sec) * 1000000000 + 
								(curr_ns - last_check_ns);
		
		if (elapsed_ns >= 250000000) {  // 250ms in nanoseconds
			last_check_sec = curr_sec;
			last_check_ns = curr_ns;
			
			
			
			if (can_terminate && total_requests >= 5 && !has_resources && (rand() % 100 < 20)) {
				struct oss_message msg;
				memset(&msg, 0, sizeof(msg));
				msg.mtype = getpid();
				msg.command = TERMINATE;
				
				if (msgsnd(msqid, &msg, sizeof(msg) - sizeof(long), 0) != -1) {
					struct worker_message response;
					memset(&response, 0, sizeof(response));
					if (msgrcv(msqid, &response, sizeof(response) - sizeof(long), getpid(), 0) != -1) {
						break;
					}
				}
			}
		}

		// Randomly choose between requesting and releasing
		if (rand() % 100 < 80) {  // 80% chance to request
			// Request 1-2 resources at a time
			int num_requests = (rand() % 2) + 1;
			for (int i = 0; i < num_requests && total_requests < 20; i++) {
				int resourceId = rand() % NUM_RESOURCES;
				if (myResources[resourceId] < (NUM_INSTANCES / 3)) {  // Limit to 1/3 of instances
					struct oss_message msg;
					memset(&msg, 0, sizeof(msg));
					msg.mtype = getpid();
					msg.command = REQUEST_RESOURCE;
					msg.resourceId = resourceId;
					total_requests++;
					
					if (msgsnd(msqid, &msg, sizeof(msg) - sizeof(long), 0) != -1) {
						struct worker_message response;
						memset(&response, 0, sizeof(response));
						if (msgrcv(msqid, &response, sizeof(response) - sizeof(long), getpid(), 0) != -1) {
							if (response.status == 1) {
								myResources[resourceId]++;
							}
						}
					}
				}
			}
		} else if (has_resources) {  // 20% chance to release if we have resources
			// Release 1 resource at a time
			for (int resourceId = 0; resourceId < NUM_RESOURCES; resourceId++) {
				if (myResources[resourceId] > 0 && (rand() % 100 < 50)) {  // 50% chance to release each held resource
					struct oss_message msg;
					memset(&msg, 0, sizeof(msg));
					msg.mtype = getpid();
					msg.command = RELEASE_RESOURCE;
					msg.resourceId = resourceId;
					
					if (msgsnd(msqid, &msg, sizeof(msg) - sizeof(long), 0) != -1) {
						myResources[resourceId]--;
					}
				}
			}
		}
	}

	shmdt(simClock);
	return 0;
}


