#include "storage_manager.h"
#include "endecode_helper.h"
#include "gateway.h"
#include "logger.h"

static storagemgr_t *p_stormgr;
static sbuffer_t *p_sbuf;

static const char* TAG="STOR";

int stormgr_start(storagemgr_t *stormgr, sbuffer_t *sbuf)
{
    if(stormgr == NULL || sbuf == NULL)
        return -1;
    p_stormgr = stormgr;
    p_sbuf = sbuf;
    return 0;
}

void stormgr_read()
{
    sensor_data_t sensor;
    ldebug("STOR: Start to read sbuf");
    if(sbuffer_read(p_sbuf, &sensor, READER_STORAGE) == 0)
    {
        printf("STOR-----------------------\n");
        printf("Retrieved sensor data\n");
        printf("ID:%u\n", sensor.id);
        printf("Type:%s \n", sensor_type_to_str(sensor.type));
        printf("Value:%.2f \n", sensor.value);
        printf("Timestamp:%ld \n", sensor.ts);
        printf("STOR-----------------------\n");
    }

}
