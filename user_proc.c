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
#define MAX_RESOURCES_PER_PROCESS 3
#define MAX_REQUESTS 15

SimulatedClock *simClock;
int shmid;
int msqid;
volatile sig_atomic_t terminating = 0;
int myResources[NUM_RESOURCES] = {0};


void handle_sigterm(int sig) {
	terminating = 1;
	printf("Process %d received SIGTERM, exiting.\n", getpid());
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
	if (terminating) {
		return false;
	}

	//Validate command
	if (command != REQUEST_RESOURCE && command != RELEASE_RESOURCE && command != TERMINATE) {
		return false;
	}

	//Initialize message structure
	struct oss_message msg;
	memset(&msg, 0, sizeof(struct oss_message));
	msg.mtype = getpid();
	msg.command = command;
	msg.resourceId = resourceId;

	//Send message
	if (msgsnd(msqid, &msg, sizeof(struct oss_message) - sizeof(long), 0) == -1) {
		return false;
	}

	// Only wait for response on resource requests/releases
	if (command != TERMINATE) {
		struct worker_message response;
		memset(&response, 0, sizeof(struct worker_message));
		
		// Wait for response
		if (msgrcv(msqid, &response, sizeof(struct worker_message) - sizeof(long), getpid(), 0) == -1) {
			return false;
		}
		return response.status == 1;
	}	
	return true;  
}

void cleanup_resources() {
	// Release all held resources
	for (int i = 0; i < NUM_RESOURCES; i++) {
		while (myResources[i] > 0) {
			struct oss_message msg;
			msg.mtype = getpid();
			msg.command = RELEASE_RESOURCE;
			msg.resourceId = i;
			
			// Don't wait for response during cleanup
			if (msgsnd(msqid, &msg, sizeof(struct oss_message) - sizeof(long), IPC_NOWAIT) == -1) {
				if (errno != EAGAIN) {  // Ignore if message queue is full
					perror("msgsnd cleanup");
				}
			}
			myResources[i]--;
		}
	}
	
	// Send final terminate message
	struct oss_message msg;
	msg.mtype = getpid();
	msg.command = TERMINATE;
	msg.resourceId = 0;
	
	if (msgsnd(msqid, &msg, sizeof(struct oss_message) - sizeof(long), IPC_NOWAIT) == -1) {
		if (errno != EAGAIN) {
			perror("msgsnd terminate");
		}
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
		detach_shared_memory();
		return 1;
	}

	//Track resources and start time
	int myResources[NUM_RESOURCES] = {0};
	//unsigned int start_sec = simClock->seconds;
	//unsigned int start_ns = simClock->nanoseconds;
	int total_requests = 0;
	int consecutive_denials = 0;
	int total_held = 0;
	//bool has_waited = false;
	int operations_since_last_release = 0;

	// Initial delay to stagger processes
	usleep(rand() % 100000);

	//Main process loop
	while (!terminating) {

		// Count resources currently held
		total_held = 0;
		for (int i = 0; i < NUM_RESOURCES; i++) {
			total_held += myResources[i];
		}

		// Check if we should terminate normally
		if (total_requests >= 5 && total_held == 0) {
			if (rand() % 100 < 25) { // 25% chance to terminate when holding no resources
				if (send_message(TERMINATE, 0)) {
					break;  // Normal termination
				}
			}
		}

		// Main resource management
		if (total_requests < MAX_REQUESTS) {
			if (total_held == 0 || (total_held < MAX_RESOURCES_PER_PROCESS && rand() % 100 < 75)) {
				// Request a resource
				int resourceId = rand() % NUM_RESOURCES;
				if (total_held >= MAX_RESOURCES_PER_PROCESS || myResources[resourceId] >= 2) {
					// Skip request and maybe try to release instead
					continue;
				}
				total_requests++;
				if (send_message(REQUEST_RESOURCE, resourceId)) {
					myResources[resourceId]++;
					total_held++;
					consecutive_denials = 0;
					operations_since_last_release++;
					
					// Hold the resource for a while
					usleep(rand() % 500000 + 100000);
				} else {
					consecutive_denials++;
					// Wait after denial, increasing wait time with consecutive denials
					usleep((rand() % 500000) * (consecutive_denials + 1));
				}
			} else if (operations_since_last_release >= 3 || total_held == MAX_RESOURCES_PER_PROCESS) {
				// Release a resource after some operations or when at max
				int resourceId;
				do {
					resourceId = rand() % NUM_RESOURCES;
				} while (myResources[resourceId] == 0);

				if (send_message(RELEASE_RESOURCE, resourceId)) {
					myResources[resourceId]--;
					total_held--;
					operations_since_last_release = 0;
				}
			}
		} else if (total_held > 0) {
			// Release all resources if we've hit max requests
			for (int i = 0; i < NUM_RESOURCES; i++) {
				while (myResources[i] > 0) {
					if (send_message(RELEASE_RESOURCE, i)) {
						myResources[i]--;
						total_held--;
						usleep(10000); // Small delay between releases
					}
		
				}
			}
		}

		// Delay between operations
		usleep(rand() % bound_B + 50000);
	}
	
	// Final cleanup if terminated by signal
	if (terminating) {
		cleanup_resources();
	}

	detach_shared_memory();
	return 0;
}


