#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include "logger.h"

static uint64_t g_seq = 0;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static int *g_p_logpipefifo_fd;

void gw_log_write(const char *msg)
{
    int fd;
    time_t now;
    struct tm tm_info;
    char log_buf[512];
    int len;

    if (msg == NULL)
        return;
    pthread_mutex_lock(&g_log_mutex);
    fd = open(LOG_FILENAME, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }
    g_seq++;
    now = time(NULL);
    localtime_r(&now, &tm_info);
    len = snprintf(log_buf,
            sizeof(log_buf),
            "%lu %04d-%02d-%02d %02d:%02d:%02d %.200s\n",
            (unsigned long)g_seq,
            tm_info.tm_year + 1900,
            tm_info.tm_mon + 1,
            tm_info.tm_mday,
            tm_info.tm_hour,
            tm_info.tm_min,
            tm_info.tm_sec,
            msg);
    if (len > 0) {
        write(fd, log_buf, (size_t)len);
    }
    close(fd);
    pthread_mutex_unlock(&g_log_mutex);
}

void ldebug(const char *TAG, const char* str, ...)
{
#ifdef DEBUG
    char s[100] = {0};
    if(strcmp(TAG, "") == 0)
        return;
    if(TAG != NULL) {
        strcat(s, TAG);
        strcat(s, " - ");
    }
    va_list ParamList;
    va_start(ParamList, str);
    int r = vsnprintf(s + strlen(s), sizeof(s), str, ParamList);
    if(r > 0) {
        printf("%s\n", s);
    }
    else {
        printf("Error when print debug, r:%d\n", r);
    }
    va_end(ParamList);
#else
    (void)TAG;
    (void)str;
#endif
}

static int ipc_write_logprocess(const char *msg)
{
    ssize_t wlen;
    if(*g_p_logpipefifo_fd == -1) {
        return -1;
    }
    /* use strlen(msg) instead of strlen(msg) + 1 because 
     * we use terminated character \n to split out message, 
     * not NULL character */
    wlen = write(*g_p_logpipefifo_fd, (const void*)msg, strlen(msg));
    if(wlen == -1) {
        const char errpipe[] = "Fail to write data to pipe";
        int err = close(*g_p_logpipefifo_fd);
        if (err == -1) {
            const char errclose[] = "Fail to close log file fifo";
            write(STDOUT_FILENO, errclose, sizeof(errclose) - 1);
            return -1;
        }
        write(STDOUT_FILENO, errpipe, sizeof(errpipe) - 1);
        return -1;

    }
    write(*g_p_logpipefifo_fd, (const void*)"\n", 1);
    return 0;
}

void ipc_log_event(const char *fmt, ...)
{
    char msg[100];
    va_list args;

    va_start(args, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* The IPC implementation owns delivery/error reporting. */
    if(ipc_write_logprocess(msg) != 0){
        fprintf(stderr, "Error: Fail to write log process pipe\n");
    }
}

void ipc_log_start(int *logpipefifo_fd)
{
    g_p_logpipefifo_fd = logpipefifo_fd;
}

void dbg_buf(const char* bufname,
    uint8_t *buf,
    size_t bufsize)
{
    printf("buffer name:%s\n", bufname);
    for(unsigned long i = 0; i < bufsize; i++) {
        printf("0x%x ", buf[i]);
    }
    printf("\n");
}
