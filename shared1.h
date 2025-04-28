//Author: Tu Le
//CS4760

#ifndef SHARED_H
#define SHARED_H
#include <sys/types.h>

struct sim_clock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

struct oss_message {
    long mtype;
    int command; // 1=request, 2=release, 3=terminate
    int resourceId;
};

struct worker_message {
    long mtype;
    int status;
};

struct process_table_entry {
    pid_t pid;
};

#endif

