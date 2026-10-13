#ifndef _EN_DECODE_HELPER_H
#define _EN_DECODE_HELPER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sensor_types.h"
#include "protocol_adapter.h"

#define SGP_MAGIC       0x7E
#define SGP_FRAME_SIZE  11
#define TBP_FRAME_SIZE 18
#define TLV_MOTION_FRAME_SIZE 8
#define TLV_DOOR_FRAME_SIZE 9
#define HLP_FRAME_SIZE 64

typedef struct
{
    sensor_id_t id;
    sensor_type_t type;
    uint16_t ppm;
    uint32_t ts;
} sgp_data_t;

ssize_t encode_tbp( uint8_t *buf, size_t bufsize, uint16_t id, double value, int64_t ts);
ssize_t encode_hlp( uint8_t *buf, size_t bufsize, uint16_t id, double value, int64_t ts);
ssize_t encode_motion_tlv( uint8_t *buf, size_t bufsize, uint16_t id, uint32_t ts);
ssize_t encode_door_tlv( uint8_t *buf, size_t bufsize, uint16_t id, uint8_t state, uint32_t ts);
ssize_t encode_sgp( uint8_t *buf, size_t bufsize, uint16_t id, uint16_t ppm, uint32_t ts);

parse_result_t parse_hlp(const uint8_t *buf, size_t len, sensor_data_t *out, size_t *consumed);
parse_result_t parse_tbp(const uint8_t *buf, size_t len, sensor_data_t *out, size_t *consumed);
parse_result_t parse_evp(const uint8_t *buf, size_t len, sensor_data_t *out, size_t *consumed);
parse_result_t parse_sgp(const uint8_t *buf, size_t len, sensor_data_t *out, size_t *consumed);
const char* sensor_type_to_str(sensor_type_t type);
ssize_t sgp_encode(
    uint16_t id,
    uint16_t ppm,
    uint32_t ts,
    uint8_t frame[SGP_FRAME_SIZE]);

int sgp_decode(
    const uint8_t *buf,
    size_t length,
    sgp_data_t *out);
#endif // _EN_DECODE_HELPER_H
