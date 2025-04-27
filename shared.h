//Author: Tu Le
//CS4760 Project 5
#include <sys/types.h> //Include for pid_t
#ifndef SHARED_H_
#define SHARED_H_

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

#endif
