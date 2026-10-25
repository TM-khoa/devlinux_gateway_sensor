#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include "logger.h"
#include <stdint.h>
#include "gateway.h"

#define THREAD_NUM 3
#define FIFO_PATH "./bin/logFifo"
#define MAX_LOG_LEN 200

#ifdef LOG_MAIN
static const char* TAG="MAIN";
#else
static const char* TAG="";
#endif


typedef void* (*pthreadfunc)(void*);
static pthread_t threadConnmgr;
static pthread_t threadDatamgr;
static pthread_t threadStoragemgr;
static pthread_mutex_t g_sigmutex;
static int g_logpipefifo_fd = -1;
static gateway_t gw;
int g_connmgr_stoppipefd[2];

void* thread_connmgr(void* arg) {
    (void)arg;
    LDB("%s created", __func__);
    if (pipe(g_connmgr_stoppipefd) == -1) {
        fprintf(stderr, "Error: fail to create pipe g_connmgr_stoppipefd\n");
    }
    fcntl(g_connmgr_stoppipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(g_connmgr_stoppipefd[1], F_SETFL, O_NONBLOCK);
    ipc_log_event("CONN Created");
    connmgr_start(&gw.conn, gw.base_port, &gw.sbuf);
    while(1) {
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            LDB("CONN get signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    LDB("connmgr terminated");
    ipc_log_event("CONN terminated");
    return NULL;
}

void* thread_datamgr(void* arg) {
    (void)arg;
    sensor_data_t sensor;
    ipc_log_event("DATA Created");
    datamgr_start(&gw.data, &gw.sbuf, SENSOR_CONFIG_PATH);
    while(1) {
        int rc = sbuffer_read(&gw.sbuf, &sensor, READER_DATA);
        if(rc != 0) {
            (void)fprintf(stderr, "[DATA] Fail to read from sbuffer\n");
        }
        datamgr_process_data(&sensor);
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            LDB("DATA get signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    LDB("datamgr terminated");
    ipc_log_event("DATA terminated");
    return NULL;
}

void* thread_storagemgr(void* arg) {
    (void)arg;
    ipc_log_event("STOR Created");
    if (stormgr_start(&gw.stor, DATABASE_NAME, &gw.sbuf) != 0) {
        LDB("Error: Fail to start storage manager");
        exit(EXIT_FAILURE);
    }

    /*
     * DB connection established.
     * Now we are allowed to read sensor data.
     */
    while (1) {
        sensor_data_t sensor;
        if (sbuffer_read(
                    &gw.sbuf,
                    &sensor,
                    READER_STORAGE) == 0) {
            LDB("Get sensor data, put to database");
            stormgr_insert(&sensor);
        }
        else { usleep(10000); }
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            LDB("STOR get signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    stormgr_stop();
    LDB("storemgr terminated");
    ipc_log_event("STOR terminated");
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

    LDB("Gateway started...");
    ipc_log_event("MAIN Gateway started...");
    for(int i = 0; i < THREAD_NUM; i++) {
        if (pthread_create(thread_list[i], NULL, pthreadfunc_list[i], (void*)&fd) != 0) {
            perror("Failed to create thread");
            return 1;
        }

    }

    while(1) {
        pthread_mutex_lock(&g_sigmutex);
        if(gw.stop == 1) {
            LDB("main receive signal");
            pthread_mutex_unlock(&g_sigmutex);
            break;
        }
        pthread_mutex_unlock(&g_sigmutex);
    }
    sbuffer_shutdown(&gw.sbuf);
    sbuffer_destroy(&gw.sbuf);
    LDB("main - join thread ");

    for(int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(*thread_list[i], NULL) != 0) {
            perror("Failed to join thread");
            return 1;
        }
    }


    ipc_log_event("MAIN Gateway is shutting down (receive signal handler)");
    LDB("Gateway is shutting down");
    sleep(1); // Wait for child process to complete write log
    return 0;

}


int check_net_port(char *str, uint16_t *port)
{
    char *endptr;
    // Reset errno before the call
    errno = 0;
    // Perform conversion
    long val = strtol(str, &endptr, 10);

    // Check for overflow or underflow errors
    if ((errno == ERANGE && (val == 0xFFFF || val == 0x00)) || (errno != 0 && val == 0)) {
        perror("Conversion error");
        return -1;
    }

    if (str == endptr) {
        printf("Error: No digits found in the string.\n");
        return -1;
    }

    // Check if there are trailing invalid characters left over
    if (*endptr != '\0') {
        printf("Partial conversion: Found number %ld, but hit invalid characters: '%s'\n", val, endptr);
    } else {
        printf("Success: Entire string converted cleanly to %ld\n", val);
    }

    *port = val;
    return 0;

}

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "\nError: Must provide exactly two arguments for sensor base port and http port\n");
        exit(EXIT_FAILURE);
    }
    if(check_net_port(argv[1], &gw.base_port) == -1) {
        fprintf(stderr, "\nError: Invalid string argument for base port\n");
        exit(EXIT_FAILURE);
    }

    if(check_net_port(argv[2], &gw.http_port) == -1) {
        fprintf(stderr, "\nError: Invalid string argument for HTTP port\n");
        exit(EXIT_FAILURE);
    }
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
                LDB("Logprocess: EOF, close fd now\n");
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
        ipc_log_start(&g_logpipefifo_fd);
        server_bringup(fd);
        close(fd);
        unlink(FIFO_PATH); // Remove FIFO file when done
    }
    return 0;
}

