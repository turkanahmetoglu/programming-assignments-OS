#include <semaphore.h>
#include <iostream>
#include <vector>
#include <unistd.h>
#include "Tour.h"

using namespace std;

void Tour::start(){
    sleep(2);
}



Tour* tour = nullptr;
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
int counter = 0;

void dummy_thread() {
    int counter_;
    pthread_mutex_lock(&mtx);
    if(counter / 2){
        counter_ = counter++;
        pthread_mutex_unlock(&mtx);
        sleep(counter_);
    }
    else {
        counter++;
        pthread_mutex_unlock(&mtx);
    }
    tour->arrive();
    tour->start();
    tour->leave();

}


int main(int argc, char *argv[]){
    int studentNum = atoi(argv[1]);
    int groupSize = atoi(argv[2]);
    int tourGuidePresent = atoi(argv[3]);
    vector<pthread_t> allThreads;
    try {
        tour = new Tour(groupSize, tourGuidePresent);
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
        return 0;
    }

    for(int i=0;i<studentNum;i++){
        pthread_t thread;
        pthread_create(&thread,NULL,(void *(*)(void *))dummy_thread,NULL);
        allThreads.push_back(thread);
    }
    for(int i=0;i<allThreads.size();i++)
        pthread_join(allThreads[i],NULL);
    printf("The Main terminates.\n");
    return 0;
}