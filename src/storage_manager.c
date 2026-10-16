#include "storage_manager.h"
#include "endecode_helper.h"
#include "gateway.h"
#include "logger.h"
#include "storage_manager.h"
#include <string.h>
#include <unistd.h>

static storagemgr_t *p_stormgr;
static sbuffer_t *p_sbuf;

static const char* TAG="STOR";
const char *sentable = \
"CREATE TABLE IF NOT EXISTS SensorData (\
id INTEGER NOT NULL, \
type INTEGER NOT NULL, \
value REAL NOT NULL, \
ts INTEGER NOT NULL \
);";

const char *seninsert = "INSERT INTO SensorData (id, type, value, ts) VALUES (?, ?, ?, ?);";


static int storage_create_table();
static int storage_connect();


static int storage_connect()
{
    int rc;

    /* If the database doesn't found, raise an error */
    rc = sqlite3_open_v2(p_stormgr->db_path, &p_stormgr->db,SQLITE_OPEN_READWRITE, NULL);

    if (rc != SQLITE_OK) {
        fprintf(stderr,
                "SQLite open failed: %s\n",
                sqlite3_errmsg(p_stormgr->db));
        LDB("SQLite open failed, rc:%d msg:%s", rc, sqlite3_errmsg(p_stormgr->db));

        if (p_stormgr->db) {
            sqlite3_close(p_stormgr->db);
            p_stormgr->db = NULL;
        }

        return -1;
    }
    p_stormgr->state = SQL_CONNECTED;
    ipc_write_logprocess("Connection to SQL server established.");
    LDB("Database connected, rc:%d", rc);

    return 0;
}

int stormgr_stop() 
{
    LDB("Close database");
    /* must save db pointer to local pointer before set to NULL */
    sqlite3 *db = p_stormgr->db;
    p_stormgr = NULL;
    p_sbuf = NULL;
    return sqlite3_close(db);
}

static int storage_create_table()
{
    char *errmsg = NULL;

    int rc = sqlite3_exec(
        p_stormgr->db,
        sentable,
        NULL,
        NULL,
        &errmsg);

    if (rc != SQLITE_OK) {

        fprintf(stderr,
                "Create table failed: %s\n",
                errmsg);

        LDB("Fail to create table:%s", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    ipc_write_logprocess("New table SensorData created.");
    return 0;
}

static int storage_prepare()
{
    int rc;

    rc = sqlite3_prepare_v2(
            p_stormgr->db,
            seninsert,
            -1,
            &p_stormgr->insert,
            NULL);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr,
                "Prepare insert failed: %s\n",
                sqlite3_errmsg(p_stormgr->db));

        return -1;
    }

    return 0;
}

int stormgr_start(storagemgr_t *stormgr, 
        const char *db_path, 
        sbuffer_t *sbuf)
{
    if(stormgr == NULL || sbuf == NULL || db_path == NULL) {
        ldebug(TAG ,"stormgr or sbuf or db_path is NULL");
        return -1;
    }
    p_stormgr = stormgr;
    p_sbuf = sbuf;

    memset(p_stormgr, 0, sizeof(*p_stormgr));
    p_stormgr->db_path = db_path;
    p_stormgr->attempts = 0;
    p_stormgr->state = SQL_CONNECTED;

    while (p_stormgr->attempts < MAX_DB_ATTEMPTS)
    {
        printf("Connecting to DB... (%d/%d)\n",
               p_stormgr->attempts + 1,
               MAX_DB_ATTEMPTS);

        if (storage_connect() == 0)
        {
            if (storage_create_table() != 0) {
                LDB("Error: Create table failed");
                break;
            }

            if (storage_prepare() != 0) {
                LDB("Error: Prepare statement failed");
                break;
            }
            return 0;
        }

        p_stormgr->attempts++;
        LDB("Try to connect again in:%ds, attempts:%d",  RETRY_DELAY_SEC, p_stormgr->attempts);

        sleep(RETRY_DELAY_SEC);
    }

    p_stormgr->state = SQL_DOWN;
    ipc_write_logprocess("Unable to connect to SQL server.");
    fprintf(stderr,
            "Unable to connect database after %d attempts\n",
            MAX_DB_ATTEMPTS);

    return -1;
}

int stormgr_insert(const sensor_data_t *sensor)
{
    int rc;

    sqlite3_reset(p_stormgr->insert);
    sqlite3_clear_bindings(p_stormgr->insert);

    sqlite3_bind_int(
        p_stormgr->insert,
        1,
        sensor->id);

    sqlite3_bind_int(
        p_stormgr->insert,
        2,
        sensor->type);

    sqlite3_bind_double(
        p_stormgr->insert,
        3,
        sensor->value);

    sqlite3_bind_int64(
        p_stormgr->insert,
        4,
        sensor->ts);

    rc = sqlite3_step(p_stormgr->insert);

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr,
                "Insert failed: %s\n",
                sqlite3_errmsg(p_stormgr->db));

        return -1;
    }

    return 0;
}
