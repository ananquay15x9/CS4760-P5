//Author: Tu Le
//CS4760 Project 5
#include <sys/types.h> //Include for pid_t
#ifndef SHARED_H_
#define SHARED_H_

//Resource management constants
#define NUM_RESOURCES 5
#define NUM_INSTANCES 10
#define REQUEST_RESOURCE 1
#define RELEASE_RESOURCE 2
#define TERMINATE 3

#pragma pack(push, 1)


typedef struct {
	unsigned int seconds;
	unsigned int nanoseconds;
} SimulatedClock;

typedef struct {
	int occupied;
	pid_t pid;
	int startSeconds;
	int startNano;
	int serviceTimeSeconds;
	int serviceTimeNano;
	int waitTimeSeconds;
	int waitTimeNano;
	int blockedTimeSeconds;
	int blockedTimeNano;
	int currentQueue;
	int queue;
} PCB;

typedef struct {
	int totalInstances; //Total number of instances of this resource
	int availableInstances; //Number of instances currently availbale
	int allocated[18]; //Instances allocated to each process (index =PCB index)
	//int requestQueue[18]; //queue of processes waiting for this resource
} ResourceDescriptor;

//Message structures for OSS <-> worker communication
struct oss_message {
	long mtype; // PID of the destination process
	int command;
	int resourceId;
};

struct worker_message {
	long mtype; // PID of the destination process (user_proc)
	int status;
};

#pragma pack(pop)

#endif
