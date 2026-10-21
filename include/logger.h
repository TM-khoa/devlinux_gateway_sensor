#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <stdint.h>
#include <stdio.h>
#define LOG_FILENAME "gateway.log"
#define LDB(STR, ...) (ldebug(TAG, (STR), ##__VA_ARGS__))

void gw_log_write(const char *msg);
void ldebug(const char *TAG, const char* str, ...);
void ipc_log_event(const char *fmt, ...);
void ipc_log_start(int *logpipefifo_fd);
void dbg_buf(const char* bufname,
    uint8_t *buf,
    size_t bufsize);


#endif // _LOGGER_H_
