#ifndef _LOGGER_H_
#define _LOGGER_H_


#define LOG_FILENAME "gateway.log"
void gw_log_write(const char *msg);
void ldebug(const char* str, ...);

#endif // _LOGGER_H_
