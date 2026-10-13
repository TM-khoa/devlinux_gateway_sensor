
#ifndef _COMMON_H_
#define _COMMON_H_

/* ---- Hằng số và kiểu liệt kê dùng chung ---- */
#define RX_BUF_SIZE 256
#define MAX_CONN 64
#define MAX_SENSORS 64
typedef enum { STATE_COLLECTING, STATE_NORMAL, STATE_ALERT } sensor_state_t;
typedef enum { SQL_CONNECTED, SQL_LOST, SQL_DOWN } sql_state_t;
#endif // _COMMON_H_
