#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include "logger.h"
#include <stdint.h>

#include "gateway.h"

#define THREAD_NUM 3
#define FIFO_PATH "./bin/logFifo"
#define MAX_LOG_LEN 200

typedef void* (*pthreadfunc)(void*);
static pthread_t threadConnmgr;
static pthread_t threadDatamgr;
static pthread_t threadStoragemgr;

static pthread_mutex_t g_sigmutex;
static int g_logpipefifo_fd = -1;
static uint16_t g_base_port = 0;
static gateway_t gw;
int g_connmgr_stoppipefd[2];

void ldebug(const char* str)
{
#ifdef DEBUG
    printf("DEBUG: %s", str);
#else
    (void)str;
#endif
}

int ipc_write_logprocess(const char *msg)
{
    ssize_t wlen;
    if(g_logpipefifo_fd == -1) {
        return -1;
    }
    /* use strlen(msg) instead of strlen(msg) + 1 because 
     * we use terminated character \n to split out message, 
     * not NULL character */
    wlen = write(g_logpipefifo_fd, (const void*)msg, strlen(msg));
    if(wlen == -1) {
        const char errpipe[] = "Fail to write data to pipe";
        int err = close(g_logpipefifo_fd);
        if (err == -1) {
            const char errclose[] = "Fail to close log file fifo";
            write(STDOUT_FILENO, errclose, sizeof(errclose) - 1);
            return -1;
        }
        write(STDOUT_FILENO, errpipe, sizeof(errpipe) - 1);
        return -1;

    }
    write(g_logpipefifo_fd, (const void*)"\n", 1);
    return 0;
}

void* thread_connmgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    if (pipe(g_connmgr_stoppipefd) == -1) {
        printf("Error: fail to create pipe g_connmgr_stoppipefd\n");
    }
    fcntl(g_connmgr_stoppipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(g_connmgr_stoppipefd[1], F_SETFL, O_NONBLOCK);
    ipc_write_logprocess("CONN Created");
    connmgr_start(&gw.conn, g_base_port);
    while(1) {
        ldebug("connmgr mutex lock\n");
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("connmgr get signal\n");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        ldebug("connmgr mutex unlock\n");
        pthread_mutex_unlock(&g_sigmutex);
        sleep(1);
    }
    ldebug("connmgr terminated\n");
    ipc_write_logprocess("CONN terminated");
    return NULL;
}

void* thread_datamgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    ipc_write_logprocess("DATA Created");
    while(1) {
        ldebug("datamgr mutex lock\n");
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("datamgr get signal\n");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        ldebug("datamgr mutex unlock\n");
        pthread_mutex_unlock(&g_sigmutex);
        sleep(1);
    }
    ldebug("datamgr terminated\n");
    ipc_write_logprocess("DATA terminated");
    return NULL;
}

void* thread_storagemgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    ipc_write_logprocess("STOR Created");
    while(1) {
        ldebug("storemgr mutex lock\n");
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("storemgr get signal\n");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        ldebug("storemgr mutex unlock\n");
        pthread_mutex_unlock(&g_sigmutex);
        sleep(1);
    }
    ldebug("storemgr terminated\n");
    ipc_write_logprocess("STOR terminated");
    return NULL;
}

void handle_sigint(int sig) {
    /* Note: Use only async-signal-safe functions inside handlers (like write) */
    gw.stop = 1;
    connmgr_stop();
    /* To unblock select() and stop in connmgr thread  */
    const char dummy = 1;
    write(g_connmgr_stoppipefd[1], &dummy, 1);

    const char msgint[] = "\nSignal handler:SIGINT Exiting cleanly\n";
    const char msgterm[] = "\nSignal handler:SIGTERM Exiting cleanly\n";
    switch(sig) {
        case SIGINT:
            write(STDOUT_FILENO, msgint, sizeof(msgint) - 1);
            break;
        case SIGTERM: 
            write(STDOUT_FILENO, msgterm, sizeof(msgterm) - 1);
            break;
        /* Add other signals as needed */
    }
}

int create_signal_handler()
{
    struct sigaction sa;

    /* Point the structure to our custom handler function */
    sa.sa_handler = &handle_sigint;

    /* Clear the signal mask during execution */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    /* Register the handler for SIGINT (Ctrl+C) */
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error registering SIGINT");
        return 1;
    }

    /* Register the handler for SIGTERM */
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Error registering SIGTERM");
        return 1;
    }
    return 0;
}

int server_bringup(int fd) {
    pthread_t *thread_list[THREAD_NUM]= {&threadConnmgr, &threadDatamgr, &threadStoragemgr};
    pthreadfunc pthreadfunc_list[THREAD_NUM] = {thread_connmgr, thread_datamgr, thread_storagemgr};
    pthread_mutex_init(&g_sigmutex, NULL);
    printf("Gateway started...\n");
    ipc_write_logprocess("MAIN Gateway started...");
    for(int i = 0; i < THREAD_NUM; i++) {
        if (pthread_create(thread_list[i], NULL, pthreadfunc_list[i], (void*)&fd) != 0) {
            perror("Failed to create thread");
            return 1;
        }

    }

    while(1) {
        ldebug("main mutex lock\n");
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("main receive signal\n");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        ldebug("main mutex unlock\n");
        pthread_mutex_unlock(&g_sigmutex);
        sleep(1);
    }
    ldebug("main - join thread \n");

    for(int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(*thread_list[i], NULL) != 0) {
            perror("Failed to join thread");
            return 1;
        }
    }

    ipc_write_logprocess("MAIN Gateway is shutting down");
    printf("Gateway is shutting down\n");
    sleep(1); // Wait for child process to complete write log
    return 0;

}

int main(int argc, char **argv) {

    (void)argc;
    g_base_port = atoi(argv[1]);
    /* Must create signal hanlder before forking 
     * to make child process inherit signal handler */
    if(create_signal_handler() != 0) {
        perror("Failed to create signal handler");
        exit(1);
    }
    mkfifo(FIFO_PATH, 0666); // Create named pipe
    pid_t pid = fork();

    if (pid == 0) {
        /* Child: Read from FIFO */
        int fd = open(FIFO_PATH, O_RDONLY);
        char buf[512] = {0};
        while(1) {
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if(n == 0) {
                printf("Child: EOF, close fd now\n");
                break;
            }
            /* IMPORTANT: strtok need NULL character to not cross over read buffer */
            buf[n] = '\0';
            /* Split message in buffer by newline character */
            char *tok = strtok(buf, "\n");
            while(tok != NULL) {
                gw_log_write(tok);
                tok = strtok(NULL, "\n");
            }
            printf("Child received: %s\n", buf);
            memset(buf, 0, sizeof(buf));
        }
        close(fd);
    } else {
        /* Parent: Write to FIFO */
        int fd = open(FIFO_PATH, O_WRONLY);
        if (fd == -1) {
            /* Option A: Print standard error message automatically */
            perror("Error opening file"); 
            /* Option B: Get explicit error string using errno */
            /* printf("Failed: %s\n", strerror(errno));  */
            return 1;
        }
        g_logpipefifo_fd = fd;
        server_bringup(fd);
        close(fd);
        unlink(FIFO_PATH); // Remove FIFO file when done
    }
    return 0;
}

