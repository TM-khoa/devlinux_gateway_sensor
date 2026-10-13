#ifndef _STORAGE_MANAGER_H_
#define _STORAGE_MANAGER_H_
#include <sqlite3.h>
#include "common.h"
#include "share_buffer.h"
#include <stdio.h>

/* ---- Storage manager + SQLite ---- */
typedef struct {
    sqlite3 *db;
    sqlite3_stmt *insert;
    sql_state_t state;
    int attempts;
    const char *db_path;
} storagemgr_t;

int stormgr_start(storagemgr_t *stormgr, sbuffer_t *sbuf);
void stormgr_read();
#endif // _STORAGE_MANAGER_H_
