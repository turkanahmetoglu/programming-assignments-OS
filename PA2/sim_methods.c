#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "multi_sched.h"

int stop_threads = 0;
TaskDeque** global_queues;
int finished_jobs_count = 0;
int total_jobs = 0;
pthread_mutex_t mock_print_lock = PTHREAD_MUTEX_INITIALIZER;

int executeJob(Task* task, TaskDeque* my_queue, int my_id) {
    if (task->owner != my_queue) {
        task->owner = my_queue;
        task->cache_warmed_up = 1.0;
    }

    task->task_duration -= (CYCLE * task->cache_warmed_up);
    
    if (task->task_duration <= 0) {
        pthread_mutex_lock(&mock_print_lock);
        printf("Processor %d: Finished %s\n", my_id, task->task_id);
        finished_jobs_count++;
        pthread_mutex_unlock(&mock_print_lock);
        return 1;
    }
    
    task->cache_warmed_up = (task->cache_warmed_up < 4.0) ? task->cache_warmed_up + 0.05 : 4.0;
    return 0;
}

// Helper to find tasks by ID during the second parsing pass
Task* find_task_by_id(Task** all_tasks, int count, const char* id) {
    for (int i = 0; i < count; i++) {
        if (strcmp(all_tasks[i]->task_id, id) == 0) return all_tasks[i];
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <dag_file.txt>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Failed to open file");
        return 1;
    }

    Task* all_tasks[1000]; 
    char line[1024];

    while (fgets(line, sizeof(line), file)) {
        char temp_id[256];
        int duration, num_deps;
        if (sscanf(line, "%255s %d %d", temp_id, &duration, &num_deps) >= 2) {
            Task* t = malloc(sizeof(Task));
            t->task_id = strdup(temp_id);
            t->task_duration = duration;
            t->cache_warmed_up = 1.0;
            t->owner = NULL; 
            
            t->in_degree = 0; 
            pthread_mutex_init(&t->in_degree_lock, NULL); 
            
            t->num_dependents = 0;
            t->dependents = malloc(sizeof(Task*) * (num_deps > 0 ? num_deps : 1));
            all_tasks[total_jobs++] = t;
        }
    }

    rewind(file);
    int current_idx = 0;
    while (fgets(line, sizeof(line), file)) {
        char* token = strtok(line, " \n\r");
        token = strtok(NULL, " \n\r"); // skip duration
        token = strtok(NULL, " \n\r"); // skip num_deps count

        while ((token = strtok(NULL, " \n\r")) != NULL) {
            Task* dep_task = find_task_by_id(all_tasks, total_jobs, token);
            if (dep_task) {
                all_tasks[current_idx]->dependents[all_tasks[current_idx]->num_dependents++] = dep_task;
                dep_task->in_degree++; 
            }
        }
        current_idx++;
    }
    fclose(file);

    global_queues = malloc(NUM_CORES * sizeof(TaskDeque*));
    initSharedVariables();

    // Submit ONLY root tasks (in_degree == 0) distributed evenly across cores
    int core_idx = 0;
    for(int i = 0; i < total_jobs; i++) {
        if(all_tasks[i]->in_degree == 0) {
            submitTask(global_queues[core_idx], all_tasks[i]);
            core_idx = (core_idx + 1) % NUM_CORES;
        }
    }

    printf("DAG parsed successfully. Total jobs: %d. Starting simulation...\n", total_jobs);

    pthread_t threads[NUM_CORES];
    for (int i = 0; i < NUM_CORES; i++) {
        ThreadArgs* args = malloc(sizeof(ThreadArgs));
        args->q = global_queues[i]; 
        args->id = i;
        pthread_create(&threads[i], NULL, processJobs, args);
    }

    while (finished_jobs_count < total_jobs) { 
        usleep(10000); 
    }
    stop_threads = 1;

    for (int i = 0; i < NUM_CORES; i++) { 
        pthread_join(threads[i], NULL); 
    }
    
    printf("All tasks finished, joining threads\n");
    return 0;
}