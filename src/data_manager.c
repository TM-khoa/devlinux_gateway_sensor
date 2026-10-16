#include "data_manager.h"
#include "endecode_helper.h"
#include "gateway.h"
#include "logger.h"

static datamgr_t *p_datmgr;
static sbuffer_t *p_sbuf;

static const char* TAG="DATA";

int datamgr_start(datamgr_t *datmgr, sbuffer_t *sbuf)
{
    if(datmgr == NULL || sbuf == NULL)
        return -1;
    p_datmgr = datmgr;
    p_sbuf = sbuf;
    return 0;
}

void datamgr_read()
{
    sensor_data_t sensor;
    LDB("DATA: Start to read sbuf");
    if(sbuffer_read(p_sbuf, &sensor, READER_DATA) == 0)
    {
        printf("DATA-----------------------\n");
        printf("Retrieved sensor data\n");
        printf("ID:%u\n", sensor.id);
        printf("Type:%s \n", sensor_type_to_str(sensor.type));
        printf("Value:%.2f \n", sensor.value);
        printf("Timestamp:%ld \n", sensor.ts);
        printf("DATA-----------------------\n");
    }

}
