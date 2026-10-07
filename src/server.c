#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define THREAD_NUM 3

typedef void* (*pthreadfunc)(void*);
static pthread_t threadConnmgr;
static pthread_t threadDatamgr;
static pthread_t threadStoragemgr;

volatile sig_atomic_t sflag = 0;

void ldebug(const char* str)
{
#ifdef DEBUG
    printf("DEBUG: %s", str);
#else
    (void)str;
#endif
}

void* thread_connmgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    while(sflag == 0) {

    }
    ldebug("connmgr terminated\n");
    return NULL;
}

void* thread_datamgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    while(sflag == 0) {

    }
    ldebug("datamgr terminated\n");
    return NULL;
}

void* thread_storagemgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    while(sflag == 0) {

    }
    ldebug("storemgr terminated\n");
    return NULL;
}

void handle_sigint(int sig) {
    // Note: Use only async-signal-safe functions inside handlers (like write)
    sflag = 1;
    char msg[50] = "\nSignal handler:";
    const char *msg2 = "Exiting cleanly\n";
    const char *sig_name = "UNKNOWN";
    switch(sig) {
        case SIGINT:  sig_name = "SIGINT ";  break;
        case SIGTERM: sig_name = "SIGTERM "; break;
        // Add other signals as needed
    }
    strncat(msg, sig_name, strlen(sig_name));
    strncat(msg, msg2, strlen(msg2));
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

int create_signal_handler()
{
    struct sigaction sa;

    // Point the structure to our custom handler function
    sa.sa_handler = &handle_sigint;

    // Clear the signal mask during execution
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Register the handler for SIGINT (Ctrl+C)
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error registering signal handler");
        return 1;
    }

    // Register the handler for SIGTERM
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Error registering signal handler");
        return 1;
    }
    return 0;
}

int main() {
    pthread_t *thread_list[THREAD_NUM]= {&threadConnmgr, &threadDatamgr, &threadStoragemgr};
    pthreadfunc pthreadfunc_list[THREAD_NUM] = {thread_connmgr, thread_datamgr, thread_storagemgr};
    if(create_signal_handler() != 0) {
        perror("Failed to create signal handler");
        return 1;
    }
    printf("Gateway started...\n");
    for(int i = 0; i < THREAD_NUM; i++) {
        if (pthread_create(thread_list[i], NULL, pthreadfunc_list[i], NULL) != 0) {
            perror("Failed to create thread");
            return 1;
        }

    }

    while(sflag == 0) {
    }
    ldebug("main - join thread \n");

    for(int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(*thread_list[i], NULL) != 0) {
            perror("Failed to join thread");
            return 1;
        }
    }

    printf("Gateway is shutting down\n");
    return 0;
}


