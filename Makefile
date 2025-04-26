#Author: Tu Le
#CS4760 Project 5
#Date: 4/25/2025

CC = gcc
CFLAGS = -Wall -g
OSS_TARGET = oss
USER_PROC_TARGET = user_proc 

OSS_OBJS = oss.o
USER_PROC_OBJS = user.proc.o

all: $(OSS_TARGET) $(USER_PROC_TARGET)

$(OSS_TARGET): $(OSS_OBJS)
	$(CC) $(CFLAGS) -o $(OSS_TARGET) $(OSS_OBJS) -lrt 

$(USER_PROC_TARGET): $(USER_PROC_OBJS)
	$(CC) $(CFLAGS) -o $(USER_PROC_TARGET) $(USER_PROC_OBJS) -lrt

oss.o: oss.c
	$(CC) $(CFLAGS) -c oss.c

user_proc.o: user_proc.c 
	$(CC) $(CFLAGS) -c user_proc.c

clean:
	rm -f $(OSS_TARGET) $(USER_PROC_TARGET) *.o
