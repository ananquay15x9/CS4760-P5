# CS4760 - Project 5: Resource Management and Deadlock Detection
**Git Repository:** [https://github.com/ananquay15x9/CS4760-P5](https://github.com/ananquay15x9/CS4760-P5.git)

## Project Description

This project implements a resource management module for an Operating System Simulator (oss). The system simulates concurrent worker processes requesting and releasing system resources while the master (oss) manages allocations, handles contention through deadlock detection, and performs recovery as necessary. Key features include:

* Resource allocation with a statis resource pool
* Non-blocking interprocess communication (IPC) using message queues
* Deadlock detection and recovery by terminating processes
* Shared memory clock simulation
* Comprehensive logging and system statistics tracking

## Compilation

To compile this project, navigate to the project directory and run 'make' in the terminal:

```bash
make
```

## Running

To run the project, execute ./oss with the following command-line options:

* '-h': Display help information
* '-n <num_processes>': Specify the number of total processes to launch (maximum active at once: 18)
* '-s <simul_seconds>': Specify total simulation run time limit (in seconds)
* '-i <interval_in_ms>': Interval in milliseconds between child process launches
* '-f <logfile>': Specify the log file name 
* '-v': Enable verbose loggin (more detailed output)

Example:
```bash
./oss -n 40 -s 5 -i 500 -f oss.log -v
```

## Implementation Details

1. **Resource Management**:
   - 5 distinct resources, each with 10 instances.
   - Each worker process can request up to 2 instances of a resource at a time.

2. **Worker Process Behavior:**:
   - Processes randomly decide to request, release, or terminate based on the clock and random chance.
   - Processes may hold up to 3 resources at once.
   - Processes use a random bound B (e.g., 100,000 ns) to determine when to act.

3. **Deadlock Detection and Recovery**:
   - 'oss' runs a deadlock detection algorithm every simulation second.
   - If deadlock is detected, processes are terminated one-by-one (youngest deadlocked first) until the deadlock is resolved.
   - Resources held by terminated processes are reclaimed and reallocated.

4. **Message Passing**:
   - Non-blocking message queue communication (IPC_NOWAIT).
   - Structured messages are used for resource requests, releases, and termination notifications.

5. **Loggin**:
   - All master (oss) activities are logged to both the screen and a specified logfile.
   - Logs include requests, grants, releases, wait queue additions, deadlock detection results, and resource tables.
   - When verbose mode is enabled (-v), detailed logs are produced after every resource event.
   - Log output is capped at 10,000 lines.

## Problems Encountered and Solutions

1. **Race Conditions During Message Passing**:
   - **Problem**: Occasional "unknown message" warnings.
   - **Solution**: Added additional command validation and resource ID checks before handling messages.

2. **Deadlock Detection Timing**:
   - **Problem**: Deadlock detection sometimes missed new deadlocks immediately after process terminations.
   - **Solution**: Re-run deadlock detection immediately after each process termination.

3. **Process Slot Management**:
   - **Problem**: Processes table inconsistencies after abnormal terminations.
   - **Solution**: Implemented thorough process table cleanup during waitpid checking and SIGTERM handling.

4. **Resource Accounting Errors**:
   - **Problem**: Resources were sometimes not fully reclaimed after deadlock recovery.
   - **Solution**: Carefully updated available instances and allocation matrices during termination.

5. **Logging Overhead**:
   - **Problem**: Log file growth impacted simulation speed.
   - **Solution**: Limited output to 10,000 lines and compressed verbose loggin to key points only.

## Deadlock Recovery Policy

When a deadlock is detected:
- 'oss' identifies all deadlocked processes
- It terminates one deadlocked process at a time, favoring the process with the lowest PID
- After termianting a process, it immediately reruns deadlock detection
- This continues until no deadlocks remain

## Limitations and Future Improvements

1. Improve deadlock victim selection (e.g., choose process holding the fewest resources).
2. Implement smarter process scheduling to balance resource allocation.

## Resources Cleanup

The system properly cleans up all IPC resources:
- Shared memory segments
- Message queues
- Process table entries
- Log files

Use `ipcs` command to verify no resources are left after termination.
