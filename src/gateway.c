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

void ldebug(const char* str, ...)
{
#ifdef DEBUG
    char s[100] = {0};
    va_list ParamList;
    va_start(ParamList, str);
    int r = vsnprintf(s, sizeof(s), str, ParamList);
    if(r > 0) {
        printf("%s\n", s);
    }
    else {
        printf("Error when print debug, r:%d\n", r);
    }
    va_end(ParamList);
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
    connmgr_start(&gw.conn, g_base_port, &gw.sbuf);
    while(1) {
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("CONN get signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    ldebug("connmgr terminated");
    ipc_write_logprocess("CONN terminated");
    return NULL;
}

void* thread_datamgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    ipc_write_logprocess("DATA Created");
    datamgr_start(&gw.data, &gw.sbuf);
    while(1) {
        datamgr_read();
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("DATA get signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    ldebug("datamgr terminated");
    ipc_write_logprocess("DATA terminated");
    return NULL;
}

void* thread_storagemgr(void* arg) {
    (void)arg;
    printf("%s created\n", __func__);
    ipc_write_logprocess("STOR Created");
    stormgr_start(&gw.stor, &gw.sbuf);
    while(1) {
        stormgr_read();
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("STOR get signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    ldebug("storemgr terminated");
    ipc_write_logprocess("STOR terminated");
    return NULL;
}

void handle_signal(int sig) {
    /* Note: Use only async-signal-safe functions inside handlers (like write) */
    gw.stop = 1;
    connmgr_stop();
    /* To unblock select() and stop in connmgr thread  */
    const char dummy = 1;
    write(g_connmgr_stoppipefd[1], &dummy, 1);

    const char msgint[] = "\nSIG:SIGINT Exiting cleanly\n";
    const char msgterm[] = "\nSIG:SIGTERM Exiting cleanly\n";
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
    sa.sa_handler = &handle_signal;

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
    sbuffer_init(&gw.sbuf);

    printf("Gateway started...\n");
    ipc_write_logprocess("MAIN Gateway started...");
    for(int i = 0; i < THREAD_NUM; i++) {
        if (pthread_create(thread_list[i], NULL, pthreadfunc_list[i], (void*)&fd) != 0) {
            perror("Failed to create thread");
            return 1;
        }

    }

    while(1) {
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            ldebug("main receive signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    sbuffer_shutdown(&gw.sbuf);
    sbuffer_destroy(&gw.sbuf);
    ldebug("main - join thread ");

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

