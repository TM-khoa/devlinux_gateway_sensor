#ifndef _SEVER_H_
#define _SEVER_H_
#include <pthread.h> 
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include "sensor_types.h"
#include "common.h"
#include "connection_manager.h"
#include "data_manager.h"
#include "storage_manager.h"



/* ---- sbuffer ---- */
typedef struct sbuffer_node {
    sensor_data_t data;
    bool read_by_data; /* Data manager đã đọc */
    bool read_by_storage; /* Storage manager đã đọc */
    struct sbuffer_node *next;
} sbuffer_node_t;

typedef struct {
    sbuffer_node_t *head, *tail;
    size_t size;
    bool shutdown; /* báo kết thúc */
    pthread_mutex_t lock;
    pthread_cond_t not_empty; /* đánh thức thread đọc */
} sbuffer_t;


/* ---- Shared status (shared memory) ---- */
typedef struct {
    sensor_id_t id; uint16_t room; sensor_type_t type;
    double last_value, running_avg; bool has_avg;
    sensor_state_t state; bool connected; time_t last_update;
} sensor_status_t;

typedef struct {
    pthread_mutex_t lock; /* PROCESS_SHARED + ROBUST */
    uint64_t received, processed, stored;
    uint32_t sbuffer_size;
    sql_state_t sql_state; uint8_t sql_retries;
    time_t start_time;
    uint16_t sensor_count;
    sensor_status_t sensors[MAX_SENSORS];
} gateway_status_t;

typedef struct {
    uint16_t base_port, http_port;
    sbuffer_t sbuf;
    connmgr_t conn;
    datamgr_t data;
    storagemgr_t stor;
    gateway_status_t *status; /* NULL cho tới Step 8 */
    pid_t log_pid, web_pid;
    volatile sig_atomic_t stop;
} gateway_t;

int ipc_write_logprocess(const char *msg);

#endif //_SEVER_H_
