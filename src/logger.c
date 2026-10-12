#include <time.h>
#include "logger.h"
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static uint64_t g_seq = 0;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

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

