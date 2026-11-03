#include "multi_sched.h"
#include <stdlib.h>
#include <unistd.h>

// ------------------ Deque Implementation ------------------

void submitTask(TaskDeque* q, Task* t) {
    // TO BE IMPLEMENTED: Insert safely
}

Task* fetchTask(TaskDeque* q) {
    // TO BE IMPLEMENTED: Pop from one end safely
    return NULL;
}

Task* fetchTaskFromOthers(TaskDeque* q) {
    // TO BE IMPLEMENTED: Steal from another end safely
    return NULL;
}

// ------------------ Thread Execution Loop ------------------

void* processJobs(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    TaskDeque* my_queue = args->q;
    int my_id = args->id;
    free(args);

    while (!stop_threads) {
        // TO BE IMPLEMENTED:
        // 1. Fetch task from local queue.
        // 2. If NULL, loop through global_queues to steal.
        // 3. If task acquired, call executeJob(task, my_queue, my_id).
        // 4. If executeJob returns 1 (finished), traverse task's dependents. Unlock them if in_degree hits 0.
        // 5. If executeJob returns 0, push the task back to the local queue.
        
        usleep(1000);
    }

    return NULL;
}

void initSharedVariables() {
    // TO BE IMPLEMENTED: 
    // 1. The main in sim_methods only allocates the array of POINTERS (global_queues).
    //    You MUST allocate the actual TaskDeque structs for each core here using malloc.
    // 2. Initialize your mutexes and other queue fields.
}