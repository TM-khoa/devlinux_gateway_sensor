#ifndef _SENSOR_TYPES_H_
#define _SENSOR_TYPES_H_
#include <stdint.h>
#include <time.h>
typedef uint16_t sensor_id_t;
typedef enum {
    SENSOR_TEMPERATURE = 1, /* °C */
    SENSOR_HUMIDITY = 2, /* %RH */
    SENSOR_MOTION = 3, /* 1 = motion detected */
    SENSOR_DOOR = 4, /* 0 = close, 1 = open */
    SENSOR_SMOKE_GAS = 5 /* ppm */
} sensor_type_t;

typedef struct {
    sensor_id_t id; /* unique ID */
    sensor_type_t type;
    double value; /* base on unit of type sensor */
    time_t ts; /* timestamp, in second, UTC */
} sensor_data_t;

/* ---- Sensor ---- */
typedef struct { /* một dòng của file room-sensor */
    uint16_t room;
    sensor_id_t id;
    sensor_type_t type;
} sensor_info_t;

#endif //_SENSOR_TYPES_H_
