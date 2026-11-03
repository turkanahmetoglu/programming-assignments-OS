#ifndef TOUR_H
#define TOUR_H

#include <pthread.h>
#include <semaphore.h>
#include <stdexcept>
#include <cstdio>
using namespace std;

class Tour {
private:
    int required;    // # of visitors needed (exclude guide)
    int hasGuide;
    int totalSlots;  // required + hasGuide

    bool tourStarted;
    int inside;
    int leavingCount;

    pthread_t guideThread;

    sem_t mutex;
    sem_t capacity;  // counting semaphore, init = totalSlots
    sem_t guideDone; // guide has finished speaking --> visitors can leave

public:
    Tour(int n, int g); // n --> # of visitors needed, g --> tour guide?
    ~Tour();
    void arrive();
    void start();
    void leave();
};


Tour::Tour(int n, int g) {
    if (n <= 0 || (g != 0 && g != 1)) {
        throw invalid_argument("An error occurred.");
    }

    required   = n;
    hasGuide   = g;
    totalSlots = n + g;

    tourStarted  = false;
    inside       = 0;
    leavingCount = 0;

    sem_init(&mutex,    0, 1);
    sem_init(&capacity, 0, totalSlots);
    sem_init(&guideDone,0, 0);
}


Tour::~Tour() {
    sem_destroy(&mutex);
    sem_destroy(&capacity);
    sem_destroy(&guideDone);
}


void Tour::arrive() {
    printf("Thread ID: %ld | Status: Arrived at the location.\n", pthread_self());

    // block if site is full or a tour is in progress
    sem_wait(&capacity);

    // critical section
    sem_wait(&mutex);
    inside++;

    if (hasGuide == 0) { // no guide
        if (inside == required) {
            tourStarted = true;
            printf("Thread ID: %ld | Status: There are enough visitors, the tour is starting.\n", pthread_self());
        } else {
            printf("Thread ID: %ld | Status: Only %d visitors inside, starting solo shots.\n", pthread_self(), inside);
        }
    } else { // with guide
        if (inside == totalSlots) {
            guideThread = pthread_self(); // this thread becomes the guide
            tourStarted = true;
            printf("Thread ID: %ld | Status: There are enough visitors, the tour is starting.\n", pthread_self());
        } else {
            printf("Thread ID: %ld | Status: Only %d visitors inside, starting solo shots.\n", pthread_self(), inside);
        }
    }

    sem_post(&mutex);
}


void Tour::leave() {

    // first critical section
    sem_wait(&mutex);

    if (!tourStarted) { // no tour
        printf("Thread ID: %ld | Status: My camera ran out of memory while waiting, I am leaving.\n", pthread_self());
        inside--;
        sem_post(&capacity); // restore one slot
        sem_post(&mutex);
        return;
    }

    // tour happened
    bool isGuide = hasGuide && pthread_equal(pthread_self(), guideThread);
    sem_post(&mutex);

    if (isGuide) { // guide leaves first
        printf("Thread ID: %ld | Status: Tour guide speaking, the tour is over.\n", pthread_self());
        // wake all visitors
        for (int i = 0; i < required; i++) {
            sem_post(&guideDone);
        }
    } else { // if guide exists, wait for announcement
        if (hasGuide) {
            sem_wait(&guideDone);
        }
        printf("Thread ID: %ld | Status: I am a visitor and I am leaving.\n", pthread_self());
    }

    // second critical section
    sem_wait(&mutex);
    leavingCount++;

    if (leavingCount == totalSlots) {
        printf("Thread ID: %ld | Status: All visitors have left, the new visitors can come.\n", pthread_self());
        inside       = 0;
        leavingCount = 0;
        tourStarted  = false;
        // restore full capacity for the next tour
        for (int i = 0; i < totalSlots; i++) {
            sem_post(&capacity);
        }
    }

    sem_post(&mutex);
}

#endif