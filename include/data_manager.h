#ifndef _DATA_MGR_H
#define _DATA_MGR_H

#include "sensor_types.h"
#include "common.h"

#define RUN_AVG_LENGTH 5 /* Average sampling */

/* ---- Data manager ---- */
typedef struct {
    sensor_info_t info;
    double window[RUN_AVG_LENGTH]; /* vòng đệm giá trị trung binh */
    size_t count, next;
    double running_avg;
    sensor_state_t state;
} datamgr_sensor_t;

typedef struct { datamgr_sensor_t sensors[MAX_SENSORS]; size_t n; } datamgr_t;
typedef void (*sensor_handler_t)(datamgr_t *, datamgr_sensor_t *, const sensor_data_t *);


#endif // _DATA_MGR_H
