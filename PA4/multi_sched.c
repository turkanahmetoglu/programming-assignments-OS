#include "multi_sched.h"
#include <stdlib.h>
#include <unistd.h>

// ------------------ Deque Implementation ------------------

void submitTask(TaskDeque* q, Task* t) {
    if (q == NULL || t == NULL) return;

    // create node
    TDQNode* newNode = (TDQNode*)malloc(sizeof(TDQNode));
    newNode->task = t;
    newNode->next = NULL;
    newNode->prev = NULL;

    // lock
    pthread_mutex_lock(&q->lock);

    // insert at tail
    if (q->tail == NULL) { // empty queue
        q->head = newNode;
        q->tail = newNode;
    } else {
        newNode->prev = q->tail;
        q->tail->next = newNode;
        q->tail = newNode;
    }

    q->size++;
    t->owner = q;

    // unlock
    pthread_mutex_unlock(&q->lock);
}

Task* fetchTask(TaskDeque* q) {
    if (q == NULL) return NULL;

    pthread_mutex_lock(&q->lock);

    // empty queue
    if (q->head == NULL) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    // take head
    TDQNode* node = q->head;
    Task* task = node->task;

    // move head
    q->head = node->next;

    if (q->head != NULL) {
        q->head->prev = NULL;
    } else { // queue became empty
        q->tail = NULL;
    }

    q->size--;
    pthread_mutex_unlock(&q->lock);
    free(node);

    return task;
}

Task* fetchTaskFromOthers(TaskDeque* q) {
    for (int i = 0; i < NUM_CORES; i++) {
        TaskDeque* other = global_queues[i];

        if (other == q) continue;

        pthread_mutex_lock(&other->lock);

        if (other->tail == NULL) {
            pthread_mutex_unlock(&other->lock);
            continue;
        }

        TDQNode* node = other->tail;
        Task* task = node->task;

        other->tail = node->prev;

        if (other->tail != NULL) {
            other->tail->next = NULL;
        } else {
            other->head = NULL;
        }

        other->size--;
        pthread_mutex_unlock(&other->lock);
        free(node);

        return task;
    }

    return NULL;
}

// ------------------ Thread Execution Loop ------------------

void shareTasks(TaskDeque* my_queue) {
    for (int i = 0; i < NUM_CORES; i++) {
        TaskDeque* other = global_queues[i];
        if (other == my_queue) continue;

        // check if other queue is smaller
        pthread_mutex_lock(&other->lock);
        int other_size = other->size;
        pthread_mutex_unlock(&other->lock);

        if (other_size < LOW_WATERMARK) {
            // give one task
            Task* task = fetchTask(my_queue);
            if (task != NULL) {
                submitTask(other, task);
            }
            return; // only move one task per cycle
        }
    }
}

void* processJobs(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    TaskDeque* my_queue = args->q;
    int my_id = args->id;
    free(args);

    while (!stop_threads) {
        // safely read queue size
        pthread_mutex_lock(&my_queue->lock);
        int size = my_queue->size;
        pthread_mutex_unlock(&my_queue->lock);

        // HIGH WATERMARK: give work away
        if (size > HIGH_WATERMARK) {
            shareTasks(my_queue);
        }

        // 1. Fetch task from local queue.
        Task* task = fetchTask(my_queue);

        // safely read queue size again
        pthread_mutex_lock(&my_queue->lock);
        size = my_queue->size;
        pthread_mutex_unlock(&my_queue->lock);

        // 2. If NULL or low load, try to steal.
        if (task == NULL || size < LOW_WATERMARK) {
            Task* stolen = fetchTaskFromOthers(my_queue);
            if (stolen != NULL) {
                if (task == NULL) {
                    task = stolen;          // had nothing, use stolen directly
                } else {
                    submitTask(my_queue, stolen);  // had one already, queue stolen for later
                }
            }
        }

        // if still no task, wait
        if (task == NULL) {
            usleep(1000);
            continue;
        }

        // 3. Execute job
        int finished = executeJob(task, my_queue, my_id);

        // 4. If finished, handle dependents
        if (finished) {
            for (int i = 0; i < task->num_dependents; i++) {
                Task* dep = task->dependents[i];

                pthread_mutex_lock(&dep->in_degree_lock);

                dep->in_degree--;

                if (dep->in_degree == 0) {
                    submitTask(my_queue, dep);
                }

                pthread_mutex_unlock(&dep->in_degree_lock);
            }
        }

        // 5. If not finished, requeue
        else {
            submitTask(my_queue, task);
        }
        
        usleep(1000);
    }

    return NULL;
}

void initSharedVariables() {

    for (int i = 0; i < NUM_CORES; i++) {

        // allocate queue
        global_queues[i] = (TaskDeque*)malloc(sizeof(TaskDeque));

        if (global_queues[i] == NULL) {
            perror("Failed to allocate TaskDeque");
            exit(1);
        }

        // initialize all fields
        global_queues[i]->head = NULL;
        global_queues[i]->tail = NULL;
        global_queues[i]->size = 0;

        if (pthread_mutex_init(&global_queues[i]->lock, NULL) != 0) {
            perror("Mutex init failed");
            exit(1);
        }
    }
}