//Author: Tu Le
//CS4760 Project 5
//Date: 4/25/2025

#include "shared.h" //Include the header
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/msg.h> //for message queues
#include <signal.h>

//Constants (These could also be in a header file)
#define MSGKEY 12345
#define MAX_PROCESSES 100
#define MAX_RUNTIME_SECONDS 3
#define NUM_RESOURCES 5
#define NUM_INSTANCES 10
#define REQUEST_RESOURCE 1
#define RELEASE_RESOURCE 2
#define TERMINATE 3

//Structures
typedef struct {
	unsigned int seconds;
	unsigned int nanoseconds;
} SimulatedClock;

typedef struct {
	int totalInstances;
	int availableInstances;
	int allocated[18]; //Instances allocated to each process
} ResourceDescriptor;

//Global Variables
SimulatedClock *simClock;
int shmid;
int msqid;
ResourceDescriptor resourceTable[NUM_RESOURCES];
PCB processTable[18];

// Function to initialize the resource table
void initializeResourceTable() {
	for (int i = 0; i < NUM_RESOURCES; i++) {
		resourceTable[i].totalInstances = NUM_INSTANCES
		resourceTable[i].availableInstances = NUM_INSTANCES;
		for (int j = 0; j < 18; j++) {
			resourceTable[i].allocated[j] = 0;
		}
	}
}

//Function to handle resource requests
int handleResourceRequest(int pid, int resourceId) {
	int processIndex = -1;
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid == pid) {
			processIndex = i;
			break;
		}
	}

	if (processIndex == -1) {
		fprintf(stderr, "Error: Process with PID %d not found in process table\n", pid);
		return -1; //Process not found
	}

	if (resourceTable[resourceId].availableInstances > 0) {
		resourceTable[resourceId].availableInstances--;
		resourceTable[resourceId].allocated[processIndex]++;
		return 1; //Granted
	} else {
		return 0; //denied
	}
}

// Function to handle resource releases
void handleResourceRelease(int pid, int resourceId) {
	int processIndex = -1;
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid == pid) {
			processIndex = i;
			break;
		}
	}

	if (processIndex == -1) {
		fprintf(stderr, "Error: Process with PID %d not found in process table\n", pid);
		return; //process not found
	}

	if (resourceTable[resourceId].allocated[processIndex] > 0) {
		resourceTable[resourceId].availableInstances++;
		resourceTable[resourceId].allocated[processIndex]--;
	}
}

//Message structures 
struct oss_message {
	long mtype; //PID of the destination process
	int command;
	int resourceId;
};

struct worker_message {
	long mtype; //PID of the destination process (user_proc)
	int status;
};

// Function to send a message to a worker process
int send_message_to_worker(pid_t worker_pid, int status) {
	struct worker_message worker_response;
	worker_response.mtype = worker_pid;
    	worker_response.status = status;

	if (msgsnd(msqid, &worker_response, sizeof(worker_response) - sizeof(long), 0) == -1) {
		perror("msgsnd (response to worker)");
		return -1;
	}
	return 0;
}


//Function to setup shared memory for the clock
int setup_shared_memory() {
	key_t key = ftok("oss.c", 1); //Using "oss.c" as the file for ftok
	if (key == -1) {
		perror("ftok");
		return 1;
	}

	shmid = shmget(key, sizeof(SimulatedClock), IPC_CREAT | 0666);
	if (shmid == -1) {
		perror("shmget");
		return 1;
	}

	simClock = (SimulatedClock *)shmat(shmid, NULL, 0);
	if (simClock == (SimulatedClock *) -1) {
		perror("shmat");
		return 1;
	}

	//Initialize the clock
	simClock->seconds = 0;
	simClock->nanoseconds = 0;

	return 0;
}

//Function to clean up shared memory
void cleanup_shared_memory() {
	if (simClock != (SimulatedClock *)-1) {
		shmdt(simClock);
	}
	if (shmid != -1) {
		shmctl(shmid, IPC_RMID, NULL); //Mark for destruction
	}
}

//function for resourceTable
void initializeResourceTable() {
	for (int i =0; i < NUM_RESOURCES; i++) {
		resourceTable[i].totalInstances = NUM_INSTANCES;
		resourceTable[i].availableInstances = NUM_INSTANCES;
		for (int j = 0; j < 18; j++) {
			resourceTable[i].allocated[j] = 0; //Initially, no resources are allocated
		}
	}
}

int main(int argc, char *argv[]) {
	//4. Main Loop
	int totalProcesses = 0;
	while (totalProcesses < MAX_PROCESSES) {
		//a. Increment the clock
		simClock->nanoseconds += 1000000; //increment by 1ms
		if (simClock->nanoseconds >= 1000000000) {
			simClock->seconds++;
			simClock->nanoseconds -= 1000000000;
		}

		//b. Launch child processes
		if (totalProcesses < 5) { // Launch initial 5 processes
			pid_t pid = fork();
			if (pid == 0) {
				//Child process
				char bound_B_str[20];
				sprintf(bound_B_str, "%d", 100000); // Example bound_B value
				execl("./user_proc", "user_proc", bound_B_str, NULL);
				perror("execl");
				exit(1);
			} else if (pid > 0) {
				//Parent process
				processTable[totalProcesses].pid = pid;
				totalProcesses++;
				printf("OSS: Launched child process %d\n", pid);
			} else {
				perror("fork");
			}
		}

		//c. Check for messages from user processes
		struct oss_message oss_msg;
		if (msgrcv(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0, IPC_NOWAIT) != -1) {
			// Message received
			switch (oss_msg.command) {
				case REQUEST_RESOURCE:
					printf("OSS: Received request for resource %d from process %ld\n",
						oss_msg.resourceId, oss_msg.mtype);
					int granted = handleResourceRequest(oss_msg.mtype, oss_msg.resourceId);
					send_message_to_worker(oss_msg.mtype, granted);
					break;
				case RELEASE_RESOURCE:
					printf("OSS: Received release of resource %d from process %ld\n",
						oss_msg.resourceId, oss_msg.mtype);
					handleResourceRelease(oss_msg.mtype, oss_msg.resourceId);
					break;
				case TERMINATE:
					printf("OSS: Received terminate request from process %ld\n",
						oss_msg.mtype);
					//handle terminate process;
					break;
				default: 
					fprintf(stderr, "OSS: Unknown message command %d from process %ld\n",
						oss_msg.command, oss_msg.mtype);
			}
		} else {
			if (errno != ENOMSG) {
				perror("msgrcv (oss)");
				//handle error 
			}
		}

		//d. Check for terminated child processes
		pid_t childPid;
		int status;
		while((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
			printf("OSS: Child process %d terminated\n", childPid);
			//Clean up process table entry (if needed)
			for (int i = 0; i < 18; i++) {
				if (processTable[i].pid == childPid) {
					processTable[i].pid = 0; //Or mark as not occupied
					break;
				}
			}
		}

		//e. Deadlock detection (every second)
		if (simClock->seconds % 1 == 0) {
			//Run deadlock detection algorithm 
			printf("OSS: Running deadlock detection\n");
		}

		//f. Output resource and process tables (every half second)
		if ((simClock->nanoseconds % 500000000) == 0) {
			//printResourceTable();
			//printProcessTable();
		}

		if (simClock->seconds >= MAX_RUNTIME_SECONDS)
			break;
	}

	//5. Cleanup 
	//

	return 0;
}
	//d. Check for terminated child processes
	pid_t childPid;
	int status;
	while ((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
		printf("OSS: Child process %d terminated\n", childPid);
		// Clean up process table entry (if needed)
            	for (int i = 0; i < 18; i++) {
			if (processTable[i].pid == childPid) {
				processTable[i].pid = 0; //Or mark as not occupied
				break;
			}
		}
	}

	//e. Deadlock detection (every second)
	if (simClock->seconds % 1 == 0) {
		//Run deadlock detection algorithm 
		printf("OSS: Running deadlock detection\n");
	}

	//f. Output resource and process tables (every half second)
	if ((simClock->nanoseconds % 500000000) == 0) {
		//printResourceTable();
		//printProcessTable();
	}

	if (simClock->seconds >= MAX_RUNTIME_SECONDS)
		break;
	}
	//main program logic

	cleanup_shared_memory(); //don't forget to cleanup
	return 0;
}

