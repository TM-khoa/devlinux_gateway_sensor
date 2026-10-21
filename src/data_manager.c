#include "data_manager.h"
#include "logger.h"
#include "sensor_config.h"
#include "endecode_helper.h"
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static datamgr_t *p_datmgr;
static sbuffer_t *p_sbuf;
static const char *p_config_path;

#ifdef LOG_DATAMGR
static const char* TAG="DATA";
#else
static const char* TAG="";
#endif


#define CONFIG_LINE_MAX 256
#define EMPTY_BUFFER_SLEEP_NS 10000000L /* 10 ms; prevents busy-spinning */

static datamgr_t *p_datmgr;
static sbuffer_t *p_sbuf;

static inline void reset_datamgr_sensor(datamgr_sensor_t *sensor)
{
    sensor->count = 0;
    sensor->next = 0;
    sensor->running_avg = 0.0;
    for (int i = 0; i < RUN_AVG_LENGTH; ++i) {
        sensor->window[i] = 0.0;
    }
}


static int parse_sensor_type(const char *name, sensor_type_t *type)
{
    if (name == NULL || type == NULL) {
        return -1;
    }
    if (strcmp(name, "temperature") == 0) {
        *type = SENSOR_TEMPERATURE;
    } else if (strcmp(name, "humidity") == 0) {
        *type = SENSOR_HUMIDITY;
    } else if (strcmp(name, "motion") == 0) {
        *type = SENSOR_MOTION;
    } else if (strcmp(name, "door") == 0) {
        *type = SENSOR_DOOR;
    } else if (strcmp(name, "smoke_gas") == 0) {
        *type = SENSOR_SMOKE_GAS;
    } else {
        return -1;
    }
    return 0;
}

static int sensor_id_exists(const datamgr_t *datamgr, sensor_id_t id)
{
    /* To check duplicate ID in sensor config path, because sensor ID is unique */
    size_t i;
    for (i = 0; i < datamgr->n; ++i) {
        if (datamgr->sensors[i].info.id == id) {
            return 1;
        }
    }
    return 0;
}

int datamgr_load_config()
{
    FILE *fp;
    char line[CONFIG_LINE_MAX];
    datamgr_t parsed;
    unsigned long line_no = 0;

    LDB("%s: Read data from %s", __func__, p_config_path);
    fp = fopen(p_config_path, "r");
    if (fp == NULL) {
        (void)fprintf(stderr, "[DATA] cannot open config '%s': %s\n",
                      p_config_path, strerror(errno));
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));

    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned long sensor_id;
        unsigned long sensor_value;
        char type_name[32];
        char extra;
        char *comment;
        int fields;
        sensor_type_t type;
        datamgr_sensor_t *entry;

        ++line_no;
        comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }

        /* Ignore blank and whitespace-only lines. */
        {
            char *p = line;
            while (*p != '\0' && isspace((unsigned char)*p)) {
                ++p;
            }
            if (*p == '\0') {
                continue;
            }
        }

        /* A fourth token is rejected to catch malformed config lines. */
        fields = sscanf(line, " %lu %lu %31s %c",
                        &sensor_id, &sensor_value, type_name, &extra);
        if (fields != 3 || sensor_id > UINT16_MAX ||
            sensor_value > UINT16_MAX) {
            (void)fprintf(stderr,
                          "[DATA] invalid config entry at %s:%lu\n",
                          p_config_path, line_no);
            (void)fclose(fp);
            return -1;
        }

        if (parse_sensor_type(type_name, &type) != 0) {
            (void)fprintf(stderr,
                          "[DATA] unknown sensor type '%s' at %s:%lu\n",
                          type_name, p_config_path, line_no);
            (void)fclose(fp);
            return -1;
        }

        if (parsed.n >= MAX_SENSORS) {
            (void)fprintf(stderr,
                          "[DATA] config has more than MAX_SENSORS (%d) entries\n",
                          MAX_SENSORS);
            (void)fclose(fp);
            return -1;
        }

        if (sensor_id_exists(&parsed, (sensor_id_t)sensor_value)) {
            (void)fprintf(stderr,
                          "[DATA] duplicate sensor ID %lu at %s:%lu\n",
                          sensor_value, p_config_path, line_no);
            (void)fclose(fp);
            return -1;
        }

        entry = &parsed.sensors[parsed.n++];
        entry->info.id = (sensor_id_t)sensor_value;
        entry->info.type = type;
        entry->state = STATE_NORMAL;
        LDB("Load sensor type %s with id: %u", sensor_type_to_str(entry->info.type), entry->info.id);


        /* Explicitly initialize the ring buffer, even though parsed was zeroed. */
        reset_datamgr_sensor(entry);
    }

    if (ferror(fp)) {
        (void)fprintf(stderr, "[DATA] error reading config '%s'\n", p_config_path);
        (void)fclose(fp);
        return -1;
    }
    (void)fclose(fp);

    if (parsed.n == 0) {
        (void)fprintf(stderr, "[DATA] config '%s' contains no sensors\n", p_config_path);
        return -1;
    }

    *p_datmgr = parsed;
    return 0;
}

static datamgr_sensor_t *find_configured_sensor(datamgr_t *datamgr,
                                                const sensor_data_t *data)
{
    size_t i;

    for (i = 0; i < datamgr->n; ++i) {
        datamgr_sensor_t *sensor = &datamgr->sensors[i];
        if (sensor->info.id == data->id) {
            if (sensor->info.type != data->type) {
                LDB("Invalid sensor node ID %u", (unsigned)data->id);
                ipc_log_event("DATA Received sensor data with invalid sensor node ID %u",
                          (unsigned)data->id);
                return NULL;
            }
            return sensor;
        }
    }

    LDB("Invalid sensor node ID %u", (unsigned)data->id);
    ipc_log_event("DATA Received sensor data with invalid type sensor node ID %u",
              (unsigned)data->id);
    return NULL;
}

static int add_running_average_sample(datamgr_sensor_t *sensor,
                                      double value,
                                      double *avg_out)
{
    double sum = 0.0;
    size_t i;

    if (sensor == NULL || avg_out == NULL) {
        return -1;
    }

    /* Add the new sample to the circular buffer. */
    sensor->window[sensor->next] = value;
    sensor->next = (sensor->next + 1U) % RUN_AVG_LENGTH;

    /* Increase sample count until the window is full. */
    if (sensor->count < RUN_AVG_LENGTH) {
        ++sensor->count;
    }

    /* Wait until all five samples have been collected. */
    if (sensor->count < RUN_AVG_LENGTH) {
        sensor->state = STATE_COLLECTING;
        return 0;
    }

    /* The window is full: calculate the average of five samples. */
    for (i = 0; i < RUN_AVG_LENGTH; ++i) {
        sum += sensor->window[i];
    }
    reset_datamgr_sensor(sensor);

    sensor->running_avg = sum / (double)RUN_AVG_LENGTH;
    *avg_out = sensor->running_avg;

    return 1;
}

static void handle_temperature(datamgr_t *datamgr,
                               datamgr_sensor_t *sensor,
                               const sensor_data_t *data)
{
    double avg;
    int result;

    (void)datamgr;
    if(data->value < TEMP_MIN_C || data->value > TEMP_MAX_C) {
        LDB("Invalid value of temperature sensor, value:%.2f", data->value);
        return ;
    }

    result = add_running_average_sample(sensor, data->value, &avg);

    if (result < 0) {
        return;
    }

    /* Do not check temperature thresholds until five samples exist. */
    if (result == 0) {
        LDB("Temperature sensor %u collecting samples: %zu/%d",
            (unsigned)sensor->info.id,
            sensor->count,
            RUN_AVG_LENGTH);
        return;
    }

    if (avg < (double)TEMP_COLD_THRESHOLD) {
        sensor->state = STATE_ALERT;

        ipc_log_event("DATA The sensor node with %u reports it's too cold "
                  "(running avg temperature = %.2f)",
                  (unsigned)sensor->info.id, avg);

    } else if (avg > (double)TEMP_HOT_THRESHOLD) {
        sensor->state = STATE_ALERT;

        ipc_log_event("DATA The sensor node with %u reports it's too hot "
                  "(running avg temperature = %.2f)",
                  (unsigned)sensor->info.id, avg);

    } else {
        sensor->state = STATE_NORMAL;

        LDB("Temperature sensor %u average: %.2f",
            (unsigned)sensor->info.id, avg);
    }
}

static void handle_humidity(datamgr_t *datamgr,
                            datamgr_sensor_t *sensor,
                            const sensor_data_t *data)
{
    double avg;
    int result;

    (void)datamgr;
    if(data->value < HUMIDITY_MIN || data->value > HUMIDITY_MAX) {
        LDB("Invalid value of humidity sensor, value:%.2f", data->value);
        return ;
    }

    result = add_running_average_sample(sensor, data->value, &avg);

    if (result < 0) {
        return;
    }

    /* Do not check humidity thresholds until five samples exist. */
    if (result == 0) {
        LDB("Humidity sensor %u collecting samples: %zu/%d",
            (unsigned)sensor->info.id,
            sensor->count,
            RUN_AVG_LENGTH);
        return;
    }

    if (avg < HUMID_DRY_THRESHOLD) {
        sensor->state = STATE_ALERT;

        ipc_log_event("DATA The sensor node with %u reports it's too dry "
                  "(running avg humidity = %.2f)",
                  (unsigned)sensor->info.id, avg);

    } else if (avg > HUMID_WET_THRESHOLD) {
        sensor->state = STATE_ALERT;

        ipc_log_event("DATA The sensor node with %u reports it's too humid "
                  "(running avg humidity = %.2f)",
                  (unsigned)sensor->info.id, avg);

    } else {
        sensor->state = STATE_NORMAL;

        LDB("Humidity sensor %u average: %.2f",
            (unsigned)sensor->info.id, avg);
    }
}

static void handle_motion(datamgr_t *datamgr, datamgr_sensor_t *sensor,
                          const sensor_data_t *data)
{
    (void)datamgr;
    if (data->value == (double)MOTION_DETECTED) {
        ipc_log_event("DATA The sensor node with %u detected motion",
                  (unsigned)sensor->info.id);
    } 
}

static void handle_door(datamgr_t *datamgr, datamgr_sensor_t *sensor,
                        const sensor_data_t *data)
{
    (void)datamgr;
    if (data->value == (double)DOOR_OPEN) {
        ipc_log_event("DATA The sensor node with %u reports the door is open",
                  (unsigned)sensor->info.id);
    } else if (data->value == (double)DOOR_CLOSE) {
        ipc_log_event("DATA The sensor node with %u reports the door is closed",
                  (unsigned)sensor->info.id);
    }
    else {
        fprintf(stderr, "Value of door sensor invalid:%.2f\n", data->value);
    }
}

static void handle_smoke_gas(datamgr_t *datamgr, datamgr_sensor_t *sensor,
                             const sensor_data_t *data)
{
    if((data->value < SMOKE_PPM_MIN || data->value > SMOKE_PPM_MAX)) {
        LDB("Invalid value of smoke gas sensor, value:%.2f", data->value);
        return ;
    }
    (void)datamgr;
    /* Smoke/gas readings are handled immediately (never averaged). */
    if (data->value >= SMOKE_PPM_MIN &&  data->value <= SMOKE_PPM_MAX) {
        if(data->value >= GAS_ALARM_PPM) {
            ipc_log_event("DATA The sensor node with %u reports a smoke/gas alarm (concentration = %.2f ppm)",
                    (unsigned)sensor->info.id, data->value);
        }
    } 
    else {
        fprintf(stderr, "Value of smoke sensor invalid:%.2f\n", data->value);
    }
}

int datamgr_process_data(const sensor_data_t *data)
{
    /* Map with sensor_type_t order */
    static const sensor_handler_t handlers[] = {
        NULL,
        handle_temperature,
        handle_humidity,
        handle_motion,
        handle_door,
        handle_smoke_gas
    };
    datamgr_sensor_t *sensor;
    unsigned type_index;

    if (data == NULL) {
        return -1;
    }

    /* To check sensor id and sensor type */
    sensor = find_configured_sensor(p_datmgr, data);
    if (sensor == NULL) {
        return -1;
    }

    /* Which handler function to handle incoming data */
    type_index = (unsigned)data->type;
    if (type_index >= (sizeof(handlers) / sizeof(handlers[0])) ||
        handlers[type_index] == NULL) {
        /* This normally also fails the config type comparison above. */
        ipc_log_event("DATA Received sensor data with invalid sensor node ID %u",
                  (unsigned)data->id);
        return -1;
    }

    handlers[type_index](p_datmgr, sensor, data);
    return 0;
}


int datamgr_start(datamgr_t *datamgr, sbuffer_t *sbuf, const char *config_path)
{
    if (datamgr == NULL || sbuf == NULL || config_path == NULL) {
        return -1;
    }
    p_datmgr = datamgr;
    p_sbuf = sbuf;
    p_config_path = config_path;

    /* Load config from room_sensor.txt */
    if (datamgr_load_config() != 0) {
        return -1;
    }
    LDB("Load all sensor successfully");
    return 0;
}

int datamgr_stop(void)
{
    p_datmgr = NULL;
    p_sbuf = NULL;
    p_config_path = NULL;
    return 0;
}
