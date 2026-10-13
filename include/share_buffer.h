#ifndef _SHARE_BUFFER_H_
#define _SHARE_BUFFER_H_
#include <stdbool.h>
#include "sensor_types.h"
#include <pthread.h> 
/* ---- sbuffer ---- */
typedef struct sbuffer_node {
    sensor_data_t data;
    bool read_by_data; /* Data manager has read */
    bool read_by_storage; /* Storage manager has read */
    struct sbuffer_node *next;
} sbuffer_node_t;

typedef struct {
    sbuffer_node_t *head, *tail;
    size_t size;
    bool shutdown; /* báo kết thúc */
    pthread_mutex_t lock;
    pthread_cond_t not_empty; /* đánh thức thread đọc */
} sbuffer_t;

typedef struct {
    sbuffer_node_t *current;
} sbuffer_iter_t;

typedef enum {
    READER_DATA,
    READER_STORAGE
} reader_type_t;

int sbuffer_insert(sbuffer_t *buffer,
                   const sensor_data_t *data);

int sbuffer_read(sbuffer_t *buffer,
                 sensor_data_t *data,
                 reader_type_t reader);

void sbuffer_shutdown(sbuffer_t *buffer);
void sbuffer_destroy(sbuffer_t *buffer);
int sbuffer_init(sbuffer_t *buffer);
#endif //_SHARE_BUFFER_H_

