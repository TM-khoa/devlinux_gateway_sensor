#ifndef _STORAGE_MANAGER_H_
#define _STORAGE_MANAGER_H_
#include <sqlite3.h>
#include "common.h"
#include "share_buffer.h"
#include <stdio.h>

#define MAX_DB_ATTEMPTS 3
#define RETRY_DELAY_SEC 1

/* ---- Storage manager + SQLite ---- */
typedef struct {
    sqlite3 *db;
    sqlite3_stmt *insert;
    sql_state_t state;
    int attempts;
    const char *db_path;
} storagemgr_t;

int stormgr_start(storagemgr_t *stormgr, 
        const char *db_path, 
        sbuffer_t *sbuf);

int stormgr_insert(const sensor_data_t *sensor);
int stormgr_stop();
#endif // _STORAGE_MANAGER_H_

