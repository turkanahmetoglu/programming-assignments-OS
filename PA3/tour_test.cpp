#include <semaphore.h>
#include <iostream>
#include <vector>
#include <unistd.h>
#include "Tour.h"

using namespace std;

void Tour::start(){
    sleep(3);
}



Tour* tour = nullptr;

void dummy_thread() {
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
        printf("Exception caught:  %s\n", e.what());
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