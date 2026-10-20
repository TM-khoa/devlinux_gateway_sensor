#ifndef _LOGGER_H_
#define _LOGGER_H_


#define LOG_FILENAME "gateway.log"
#define LDB(STR, ...) (ldebug(TAG, (STR), ##__VA_ARGS__))

void gw_log_write(const char *msg);
void ldebug(const char *TAG, const char* str, ...);
void ipc_log_event(const char *fmt, ...);
void ipc_log_start(int *logpipefifo_fd);


#endif // _LOGGER_H_
