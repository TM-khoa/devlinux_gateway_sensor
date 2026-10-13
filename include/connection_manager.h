#ifndef _CONN_MGR_H_
#define _CONN_MGR_H_

#include <stdint.h>
#include <stdbool.h>
#include "protocol_adapter.h"
#include "common.h"

#define NUM_PORTS 4

typedef struct {
    int fd; // To save client connection
    const protocol_adapter_t *adapter;
    uint8_t rx[RX_BUF_SIZE]; /* bộ đệm nhận riêng */
    size_t rx_len;
    bool identified; /* đã biết sensor ID */
    sensor_id_t sensor_id;
} connection_t;

typedef struct {
    int listen_fd[NUM_PORTS]; /* cổng P .. P+3 */
    connection_t conns[MAX_CONN];
    size_t n_conns;
} connmgr_t;

void connmgr_start(connmgr_t *connmgr, uint16_t baseport);
void connmgr_stop();
#endif // _CONN_MGR_H_
