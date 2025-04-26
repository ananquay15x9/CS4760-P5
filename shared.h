//Author: Tu Le
//CS4760 Project 5

#define NUM_RESOURCES 5 //number of resource types
#define NUM_INSTANCES 10 // number of instances per resource

typedef struct {
	int totalInstances; //Total number of instances of this resource
	int availableInstances; //Number of instances currently availbale
	int allocated[18]; //Instances allocated to each process (index =PCB index)
	//int requestQueue[18]; //queue of processes waiting for this resource
} ResourceDescriptor;



