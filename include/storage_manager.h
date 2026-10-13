#ifndef _STOAGE_MANAGER_H_
#define _STOAGE_MANAGER_H_
#include <sqlite3.h>
#include "common.h"

/* ---- Storage manager + SQLite ---- */
typedef struct {
    sqlite3 *db;
    sqlite3_stmt *insert;
    sql_state_t state;
    int attempts;
    const char *db_path;
} storagemgr_t;

#endif // _STOAGE_MANAGER_H_
