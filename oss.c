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
#include <stdbool.h> //Include for bool type
#include <sys/wait.h> //Include for waitpid()
#include <string.h>
#include <stdarg.h>
#include <time.h>

//Constants (These could also be in a header file)
#define MSGKEY 12345
#define MAX_PROCESSES 40
#define MAX_RUNTIME_SECONDS 3
#define NUM_RESOURCES 5
#define NUM_INSTANCES 10
#define REQUEST_RESOURCE 1
#define RELEASE_RESOURCE 2
#define TERMINATE 3
#define LOG_LINE_LIMIT 10000

// Statistics tracking
int stat_requests_granted_immediately = 0;
int stat_requests_granted_after_wait = 0;
int stat_deadlock_terminations = 0;
int stat_normal_terminations = 0;
int stat_deadlock_detection_runs = 0;
int stat_deadlock_processes_terminated = 0;


//Function prototypes
int handleResourceRequest(int pid, int resourceId);
int send_message_to_worker(pid_t worker_pid, int status);
void oss_log(const char *fmt, ...);
void cleanup_shared_memory();

//Global Variables
SimulatedClock *simClock;
int shmid;
int msqid;
ResourceDescriptor resourceTable[NUM_RESOURCES];
PCB processTable[18];
int available[NUM_RESOURCES]; //available resources
int max[18][NUM_RESOURCES]; //max demand of each process
int allocation[18][NUM_RESOURCES]; //resources currently allocated to each process
int need[18][NUM_RESOURCES]; //remaining need of each process
int verbose = 0;
int log_line_count = 0;
static int warning_count = 0;
FILE *logfile = NULL;

//Wait queue for blocked resource requests
#define MAX_WAIT_QUEUE 100

typedef struct {
	int pid;
	int resourceId;
} WaitQueueEntry;

WaitQueueEntry waitQueue[MAX_WAIT_QUEUE];
int waitQueueSize = 0;

//Add a request to the wait queue
void addToWaitQueue(int pid, int resourceId) {
	if (waitQueueSize < MAX_WAIT_QUEUE) {
		waitQueue[waitQueueSize].pid = pid;
		waitQueue[waitQueueSize].resourceId = resourceId;
		waitQueueSize++;
	} else {
		fprintf(stderr, "Wait queue is full!\n");
	}
}

//Remove a request from the wait queue by index
void removeFromWaitQueue(int index) {
	if (index < 0 || index >= waitQueueSize) return;
	for (int i = index; i < waitQueueSize - 1; i++) {
		waitQueue[i] = waitQueue[i + 1];
	}
	waitQueueSize--;
}

//Try to grant requests in the wait queue
void processWaitQueue() {
	int i = 0;
	while (i < waitQueueSize) {
		int pid = waitQueue[i].pid;
		int resourceId = waitQueue[i].resourceId;
		int granted = handleResourceRequest(pid, resourceId);
		if (granted == 1) {
			send_message_to_worker(pid, 1);
			stat_requests_granted_after_wait++;
			removeFromWaitQueue(i); //remove and don't increment i 
		} else {
			i++; //only increment if not removed
		}
	}
}

//signal handler
void sigint_handler(int sig) {
	oss_log("OSS: Caught SIGINT, cleaning up...\n");

	//kill all remanining user processes
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid != 0) {
			kill(processTable[i].pid, SIGTERM);
		}
	}

	//Wait for all children to exit
	while (wait(NULL) > 0);

	//clean up shared memory
	cleanup_shared_memory();

	//Close logfile
	if (logfile) fclose(logfile);

	exit(1);
}

//Function to check if the system is in a safe state
bool isSafe(int processId, int resourceId, int request);

// Function to initialize the resource table
void initializeResourceTable() {
	for (int i = 0; i < NUM_RESOURCES; i++) {
		resourceTable[i].totalInstances = NUM_INSTANCES;
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
		oss_log("OSS Warning: Received message from dead process %d\n", pid);
		return -1; //Process not found
	}

	//Check if enough resources available
	if (available[resourceId] < 1) {
		return 0; //can't grant
	}

	//Check if the request can be granted safely
	if (isSafe(pid, resourceId, 1)) { //assuming each request is for 1 instance
		resourceTable[resourceId].availableInstances--;
		available[resourceId]--;
		resourceTable[resourceId].allocated[processIndex]++;
		allocation[processIndex][resourceId]++; //update allocation
		need[processIndex][resourceId]--; //update need
		return 1; //Granted
	} else {
		return 0; //Denied (unsafe state)
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
		oss_log("OSS Warning:: Process with PID %d not found in process table\n", pid);
		return; //process not found
	}

	if (resourceTable[resourceId].allocated[processIndex] > 0) {
		resourceTable[resourceId].availableInstances++;
		available[resourceId]++;
		resourceTable[resourceId].allocated[processIndex]--;
	}
}

// Function to send a message to a worker process
int send_message_to_worker(pid_t worker_pid, int status) {
	struct worker_message worker_response;
	worker_response.mtype = worker_pid; //Child's PID
    	worker_response.status = 1; //granted or terminated OK

	//msgsnd(msqid, &worker_response, sizeof(worker_response) - sizeof(long), 0);

	if (msgsnd(msqid, &worker_response, sizeof(worker_response) - sizeof(long), 0) == -1) {
	printf("OSS sending: mtype=%ld, status=%d\n", worker_response.mtype, worker_response.status);
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

//Function to check if the system is in a safe state
bool isSafe(int processId, int resourceId, int request) {
	if (available[resourceId] < request) {
		return false; //Not enough available
	}

	int work[NUM_RESOURCES]; //available resources
	bool finish[18]; //indicates if a process can finish
	int temp_allocation[18][NUM_RESOURCES]; //temporary allocation matrix

	//1. Initialize work and finish
	for (int i = 0; i < NUM_RESOURCES; i++) {
		work[i] = available[i];
	}
	for (int i = 0; i < 18; i++) {
		finish[i] = false;
	}

	//2. Simulate the allocation
	work[resourceId] -= request;

	//2.5 Copy allocation to temp_allocation
	for (int i = 0; i < 18; i++) {
		for (int j = 0; j < NUM_RESOURCES; j++) {
			temp_allocation[i][j] = allocation[i][j];
		}
	}

	int processIndex = -1;
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid == processId) {
			processIndex = i;
			break;
		}
	}
	
	//Simulate allocating the resource
	temp_allocation[processIndex][resourceId] += request; //newly added

	//3. Find a process that can finish
	int count = 0;
	while (count < 18) {
		bool found = false;
		for (int i = 0; i < 18; i++) {
			if (!finish[i]) {
				bool canFinish = true;
				for (int j = 0; j < NUM_RESOURCES; j++) {
					if (need[i][j] > work[j]) {
						canFinish = false;
						break;
					}
				}
				if (canFinish) {
					for (int j = 0; j < NUM_RESOURCES; j++) {
						work[j] += temp_allocation[i][j];
					}
					finish[i] = true;
					count++;
					found = true;
				}
			}
		}
		if (!found) {
			return false; //Unsafe state
		}
	}
	return true; //Safe state
}

//Deadlock detection and recovery
void detectAndResolveDeadlock() {
	stat_deadlock_detection_runs++;
	bool finish[18];
	int work[NUM_RESOURCES];
	int temp_allocation[18][NUM_RESOURCES];
	int temp_need[18][NUM_RESOURCES];
	int deadlocked[18];
	int deadlockedCount = 0;

	//Initialize work and finish
	for (int i = 0; i < NUM_RESOURCES; i++) {
		work[i] = available[i];
	}
	for (int i = 0; i < 18; i++) {
		finish[i] = (processTable[i].pid == 0); //true if no process
		deadlocked[i] = 0;
		for (int j = 0; j < NUM_RESOURCES; j++) {
			temp_allocation[i][j] = allocation[i][j];
			temp_need[i][j] = need[i][j];
		}
	}

	//Try to find a sequence where all can finish
	bool progress = true;
	while (progress) {
		progress = false;
		for (int i = 0; i < 18; i++) {
			if (!finish[i]) {
				bool canFinish = true;
				for (int j = 0; j < NUM_RESOURCES; j++) {
					if (temp_need[i][j] > work[j]) {
						canFinish = false;
						break;
					}
				}
				if (canFinish) {
					for (int j = 0; j < NUM_RESOURCES; j++) {
						work[j] += temp_allocation[i][j];
					}
					finish[i] = true;
					progress = true;
				}
			}
		}
	}

	//Any process not finished is deadlocked
	for (int i = 0; i < 18; i++) {
		if (!finish[i] && processTable[i].pid != 0) {
			deadlocked[deadlockedCount++] = i;
		}
	}

	if (deadlockedCount == 0) {
		oss_log("OSS: Deadlock detection: No deadlocks detected at time %u:%u\n", simClock->seconds, simClock->nanoseconds);
		return;
	}

	oss_log("OSS: Deadlock detected at time %u%u. Deadlocked processes:", simClock->seconds, simClock->nanoseconds);
	for (int i = 0; i < deadlockedCount; i++) {
		oss_log(" P%d", deadlocked[i]);
	}
	oss_log("\n");

	//Termiante deadlocked processes one by one until deadlock is resolved
	for (int k = 0; k < deadlockedCount; k++) {
		int idx = deadlocked[k];
		pid_t pid = processTable[idx].pid;
		oss_log("OSS: Terminating process P%d (PID %d) to resolve deadlock\n", idx, pid);
		//Release all resources held by this process
		for (int j = 0; j < NUM_RESOURCES; j++) {
			available[j] += allocation[idx][j];
			allocation[idx][j] = 0;
			need[idx][j] = max[idx][j];
		}

		//Remove from process table
		processTable[idx].pid = 0;
		//Remove from wait queue if present
		for (int w = 0; w < waitQueueSize; ) {
			if (waitQueue[w].pid == pid) {
				removeFromWaitQueue(w);
			} else {
				w++;
			}
		}
		//send SIGTERM to the process
		kill(pid, SIGTERM);
		stat_deadlock_terminations++;
		stat_deadlock_processes_terminated++;
		//After terminating one, re-run detection to see if deadlock is resolved
		oss_log("OSS: Re-running deadlock detection after terminating P%d\n", idx);
		detectAndResolveDeadlock();
		break; // only termiante one at a time, then re-check
	}
}

//Logging helper function
void oss_log(const char *fmt, ...) {
	if (log_line_count >= LOG_LINE_LIMIT) return;

	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	if (logfile) {
		vfprintf(logfile, fmt, args);
	}
	va_end(args);
	log_line_count++;

	if (strstr(fmt, "Warning") != NULL) {
		warning_count++;
		if (warning_count > 100) exit(1); //Too many warnings
	}
}

//Helper to print resource table
void printResourceTable() {
	oss_log("Current system resources (available/total):\n");
	for (int i = 0; i < NUM_RESOURCES; i++) {
		oss_log("R%d: %d/%d  ", i, available[i], NUM_INSTANCES);
	}
	oss_log("\n");
}

// Helper to print process table
void printProcessTable() {
	oss_log("Current process allocations:\n");
    	oss_log("    ");
    	for (int j = 0; j < NUM_RESOURCES; j++) {
        	oss_log("R%d ", j);
    	}
	oss_log("\n");
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid != 0) {
			oss_log("P%-2d ", i);
			for (int j = 0; j < NUM_RESOURCES; j++) {
				oss_log("%2d ", allocation[i][j]);
			}
			oss_log("\n");
		}
	}
}

// Helper to print statistics
void printStatistics() {
	oss_log("\n==== Simulation Statistics ====\n");
    	oss_log("Requests granted immediately: %d\n", stat_requests_granted_immediately);
    	oss_log("Requests granted after waiting: %d\n", stat_requests_granted_after_wait);
    	oss_log("Processes terminated by deadlock: %d\n", stat_deadlock_terminations);
    	oss_log("Processes terminated normally: %d\n", stat_normal_terminations);
    	oss_log("Deadlock detection runs: %d\n", stat_deadlock_detection_runs);
    	oss_log("Processes terminated per deadlock event: %d\n", stat_deadlock_processes_terminated);
    	oss_log("===============================\n\n");
}

// SIGINT handler for cleanup on Ctrl+C
void handle_sigint(int sig) {
	//Send SIGTERM to all children
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid != 0) {
			kill(processTable[i].pid, SIGTERM);
		}
	}

	//Give them a moment to exit
	usleep(200000); //200ms

	//Send SIGTERM to any that remain
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid != 0) {
			kill(processTable[i].pid, SIGTERM);
		}
	}

	//Wait for all children
	while (wait(NULL) > 0);
	//Cleanup shared memory
	cleanup_shared_memory();
	//Print final statistics
    	if (logfile) fclose(logfile);
    	exit(0);
}

int main(int argc, char *argv[]) {
	//Register SIGINT handler for cleanup
	signal(SIGINT, sigint_handler);

	//4. Main Loop
	int totalProcesses = 0;

	//Command line argument parsing 
	char *logfilename = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
			logfilename = argv[++i];
		} else if (strcmp(argv[i], "-v") == 0) {
			verbose = 1;
		}
	}
	if (logfilename) {
		logfile = fopen(logfilename, "w");
		if (!logfile) {
			perror("fopen logfile");
			return 1;
		}
	}

	// Shared memory setup 
	if (setup_shared_memory()) {
		fprintf(stderr, "Failed to setup shared memory for clock\\n");
		return 1;
	}

	// Message queue setup
	msqid = msgget(MSGKEY, IPC_CREAT | 0666);
	if (msqid == -1) {
		perror("msgget");
		cleanup_shared_memory();
		exit(1);
	}

	// Initialize resource table
	initializeResourceTable();

	//Initialize available
	for (int i = 0; i < NUM_RESOURCES; i++) {
		available[i] = NUM_INSTANCES; //Initially, all instances are available
	}

	//Initialize max
	for (int i = 0; i < 18; i++) {
		for (int j = 0; j < NUM_RESOURCES; j++) {
			max[i][j] = NUM_INSTANCES / 2;
		}
	}

	//Initialize allocation
	for (int i = 0; i < 18; i++) {
		for (int j = 0; j < NUM_RESOURCES; j++) {
			allocation[i][j] = 0;
		}
	}

	//Initialize need
	for (int i = 0; i < 18; i++) {
		for (int j = 0; j < NUM_RESOURCES; j++) {
			need[i][j] = max[i][j] - allocation[i][j];
		}
	}


	printf("sizeof(struct oss_message) = %zu\n", sizeof(struct oss_message));
	printf("sizeof(struct worker_message) = %zu\n", sizeof(struct worker_message));

	while (1) {
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
				int slot = -1;
				for (int i = 0; i < 18; i++) {
					if (processTable[i].pid == 0) {
						slot = i;
						break;
					}
				}
				if (slot != -1) {
					processTable[slot].pid = pid;
					oss_log("OSS: Launched child process %d in slot %d\n", pid, slot);
					totalProcesses++;
				} else {
					oss_log("OSS: No available slot for new processes!\n");
				}
			} else {
				perror("fork");
			}
		}

		//c. Check for messages from user processes
		struct oss_message oss_msg;
		if (msgrcv(msqid, &oss_msg, sizeof(oss_msg) - sizeof(long), 0, IPC_NOWAIT) != -1) {
		printf("OSS received: mtype=%ld, command=%d, resourceId=%d\n", oss_msg.mtype, oss_msg.command, oss_msg.resourceId);

		//Validate ResourceId
		if (oss_msg.resourceId < 0 || oss_msg.resourceId >= NUM_RESOURCES) {
			oss_log("OSS Warning: Invalid resource ID %d from process %ld. Ignoring request.\n",
					oss_msg.resourceId, oss_msg.mtype);
			continue; //skip invalid message
		}

			// Message received
			switch (oss_msg.command) {
				case REQUEST_RESOURCE: {
					int granted = 0;
					if (available[oss_msg.resourceId] > 0) {
						int processIndex = -1;
						for (int i = 0; i < 18; i++) {
							if (processTable[i].pid == oss_msg.mtype) {
								processIndex = i;
								break;
							}
						}
						if (processIndex != -1) {
							available[oss_msg.resourceId]--;
							allocation[processIndex][oss_msg.resourceId]++;
							need[processIndex][oss_msg.resourceId]--;
							granted = 1;
						}
					}

					if (granted) {
						oss_log("OSS: Granted resource %d to process %ld at time %u:%u\n",
							oss_msg.resourceId, oss_msg.mtype, simClock->seconds, simClock->nanoseconds);
						send_message_to_worker(oss_msg.mtype, 1);
						stat_requests_granted_immediately++;
					} else {
						oss_log("OSS: Not enough resources for process %ld, adding to wait queue at time %u:%u\n", 
								oss_msg.mtype, simClock->seconds, simClock->nanoseconds);
						addToWaitQueue(oss_msg.mtype, oss_msg.resourceId);
					}
					break;
				}
				case RELEASE_RESOURCE: {
					int processIndex = -1;
					for (int i = 0; i < 18; i++) {
						if (processTable[i].pid == oss_msg.mtype) {
							processIndex = i;
							break;
						}
					}
					if (processIndex != -1 && allocation[processIndex][oss_msg.resourceId] > 0) {
						available[oss_msg.resourceId]++;
						allocation[processIndex][oss_msg.resourceId]--;
						need[processIndex][oss_msg.resourceId]++;
						oss_log("OSS: Process %ld released resource %d. \n", oss_msg.mtype, oss_msg.resourceId);
					}
					processWaitQueue(); //try to grant blocked requests after a release
					break;
				}
				case TERMINATE: {
					int processIndex = -1;
					for (int i = 0; i < 18; i++) {
						if (processTable[i].pid == oss_msg.mtype) {
							processIndex = i;
							break;
						}
					}
					if (processIndex != -1) {
						for (int j = 0; j < NUM_RESOURCES; j++) {
							available[j] += allocation[processIndex][j];
							allocation[processIndex][j] = 0;
							need[processIndex][j] = max[processIndex][j];
						}
						processTable[processIndex].pid = 0;
					}
					send_message_to_worker(oss_msg.mtype, 1); // Send confirmation so user_proc can exit
					stat_normal_terminations++;
					break;
				}
				default: 
					oss_log("OSS: Unknown message command %d from process %ld\n",
						oss_msg.command, oss_msg.mtype);
			}
		} else {
			if (errno != ENOMSG) {
				oss_log("OSS: msgrcv error\n");
				perror("msgrcv (oss)");
				//handle error 
			}
		}

		//d. Check for terminated child processes
		pid_t childPid;
		int status;
		while((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
			oss_log("OSS: Child process %d terminated\n", childPid);
			stat_normal_terminations++;
			//terminatedChildren++;

			//Clean up process table entry (if needed)
			for (int i = 0; i < 18; i++) {
				if (processTable[i].pid == childPid) {
					processTable[i].pid = 0; //Or mark as not occupied
					break;
				}
			}
		}

		//Move runningChildren counting here
		int runningChildren = 0;
		for (int i = 0; i < 18; i++) {
			if (processTable[i].pid != 0) runningChildren++;
		}

		//e. Deadlock detection (every second)
		if (simClock->seconds % 1 == 0 && simClock->nanoseconds == 0) {
			//Run deadlock detection algorithm 
			detectAndResolveDeadlock();
		}

		//f. Output resource and process tables (every half second)
		if ((simClock->nanoseconds % 500000000) == 0) {
			printResourceTable();
			printProcessTable();
		}
		
		//At the end of each loop, try to process the wait queue
		processWaitQueue();


		//Only fork if there are fewer than 18 running
		if (runningChildren < 18 && totalProcesses < MAX_PROCESSES) {
			pid_t pid = fork();
			if (pid == 0) {
				//Child process
				char bound_B_str[20];
				sprintf(bound_B_str, "%d", 100000); //bound_b value
				execl("./user_proc", "user_proc", bound_B_str, NULL);
				perror("execl");
				exit(1);
			} else if (pid > 0) {
				//Parent process
				int slot = -1;
				for (int i = 0; i < 18; i++) {
					if (processTable[i].pid == 0) {
						slot = i;
						break;
					}
				}
				if (slot != -1) {
					processTable[slot].pid = pid;
					oss_log("OSS: Launched child process %d in slot %d\n", pid, slot);
					totalProcesses++;
				} else {
					oss_log("OSS: No available slot for new processes!\n");
				}
			} else {
				perror("fork");
			}
		}

		//print resource and process tables every 20 grants
		if ((stat_requests_granted_immediately + stat_requests_granted_after_wait) % 20 == 0) {
    			printResourceTable();
    			printProcessTable();
    			printStatistics();
    			//last_printed_request_count = stat_requests_granted_immediately + stat_requests_granted_after_wait;
		}

		// Terminate if all children have finished or simulation time is up
		if ((totalProcesses >= MAX_PROCESSES && runningChildren == 0) || simClock->seconds >= 5) {
			oss_log("OSS: Simulation terminating at time %u:%u\n", simClock->seconds, simClock->nanoseconds);
			break;
		}
	}

	

	//5. Cleanup 
	//Send SIGTERM to all remaining children
	for (int i = 0; i < 18; i++) {
		if (processTable[i].pid != 0) {
			kill(processTable[i].pid, SIGTERM);
		}
	}
	
	//Wait for all children to exit
	while (wait(NULL) > 0);

	//Print final output
	printResourceTable();
	printProcessTable();
	printStatistics();

	cleanup_shared_memory(); //cleanup

	//At the end of main, before cleanup, print final statistics
	printStatistics();
	if (logfile) fclose(logfile);

	return 0;
}
