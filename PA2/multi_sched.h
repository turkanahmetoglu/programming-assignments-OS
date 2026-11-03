#ifndef MULTI_SCHED_H
#define MULTI_SCHED_H

#include <pthread.h>

#ifndef NUM_CORES
#define NUM_CORES 8
#endif

#define CYCLE 200

typedef struct TaskDeque TaskDeque;

typedef struct Task {
    char* task_id;
    int task_duration;
    double cache_warmed_up;
    TaskDeque* owner; 
    
    pthread_mutex_t in_degree_lock;
    int in_degree;      
    int num_dependents;        
    struct Task** dependents;  
} Task;

struct TaskDeque {
    // TODO: Implement a dequeue
};

typedef struct ThreadArgs {
    TaskDeque* q;
    int id;
} ThreadArgs;

extern int stop_threads;
extern TaskDeque** global_queues;

// ------------------ Student Implemented Functions ------------------
void submitTask(TaskDeque* q, Task* t);
Task* fetchTask(TaskDeque* q);
Task* fetchTaskFromOthers(TaskDeque* q);
void* processJobs(void* arg);
void initSharedVariables();

// ------------------ Provided Functions ------------------
// Returns 1 if task is finished, 0 if it still has duration remaining
int executeJob(Task* task, TaskDeque* my_queue, int my_id);

#endif