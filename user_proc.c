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
volatile sig_atomic_t terminating = 0;

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

	//Send messages with retries
	int retries = 3;
	while (retries > 0 && !terminating) {
		if (msgsnd(msqid, &msg, sizeof(struct oss_message) - sizeof(long), IPC_NOWAIT) != -1) {
			break;
		}
		if (errno != EAGAIN && errno != EINTR) {
			return false;
		}
		retries--;
		usleep(1000); // Short delay between retries
	}

	if (retries == 0 || terminating) {
		return false;
	}

	// Only wait for response on resource requests/releases
	if (command != TERMINATE) {
		struct worker_message response;
		memset(&response, 0, sizeof(struct worker_message));
		
		// Wait for response with timeout
		struct timespec timeout;
		timeout.tv_sec = 0;
		timeout.tv_nsec = 100000000; // 100ms timeout
		
		fd_set readfds;
		FD_ZERO(&readfds);
		int msgfd = msqid;
		FD_SET(msgfd, &readfds);

		while (!terminating) {
			ssize_t recv_size = msgrcv(msqid, &response, 
									 sizeof(struct worker_message) - sizeof(long),
									 getpid(), IPC_NOWAIT);
			
			if (recv_size == sizeof(struct worker_message) - sizeof(long)) {
				return response.status == 1;
			}
			return false;  // Consider interrupted calls as failures
		}
		
		if (errno != ENOMSG && errno != EINTR) {
				break;
			}
			
			usleep(1000); // Short delay before retry
		}
		return false;
	}
	
	return true;  
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
	unsigned int start_sec = simClock->seconds;
	unsigned int start_ns = simClock->nanoseconds;
	unsigned int last_check_sec = start_sec;
	unsigned int last_check_ns = start_ns;
	int total_requests = 0; //track total request made
	int consecutive_denials = 0;

	//Main process loop
	while (!terminating) {
		//Sleep for a shorter time to be more active
		usleep(rand() % bound_B);

		if (terminating) break;

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
					while (myResources[i] > 0 && !terminating) {
						if (send_message(RELEASE_RESOURCE, i)) {
							myResources[i]--;
						} else {
							usleep(1000);
						}
					}
				}
				if (!terminating && send_message(TERMINATE, 0)) {
					break;
				}
			}
		}

		if (terminating) break;

		// Request or release resources
		  if (total_held < 3 && total_requests < 15 && consecutive_denials < 3) {
			// Try to request a resource
			int resourceId = rand() % NUM_RESOURCES;
			if (myResources[resourceId] < 2) {  // Maximum 2 of any resource
				total_requests++; //Count attempt regardless of outcome
				if (send_message(REQUEST_RESOURCE, resourceId)) {
					myResources[resourceId]++;
					consecutive_denials = 0;
				} else {
					consecutive_denials++;
					usleep(1000); //small delay after denial
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

	// Final cleanup
	if (!terminating) {
		// Release any remaining resources
		for (int i = 0; i < NUM_RESOURCES; i++) {
			while (myResources[i] > 0) {
				if (send_message(RELEASE_RESOURCE, i)) {
					myResources[i]--;
				}
			}
		}
	}

	detach_shared_memory();
	return 0;
}


