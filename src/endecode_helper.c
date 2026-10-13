#include "endecode_helper.h"


static void write_le16(uint8_t *buf, uint16_t value);
static void write_le64(uint8_t *buf, uint64_t value);
static void write_be16(uint8_t *buf, uint16_t value);
static void write_be32(uint8_t *buf, uint32_t value);
static uint32_t read_be32(const uint8_t *buf);
static uint16_t read_be16(const uint8_t *buf);
static uint16_t read_le16(const uint8_t *buf);
static uint64_t read_le64(const uint8_t *buf);
static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length) ;

ssize_t encode_tbp(
    uint8_t *buf, size_t bufsize,
    uint16_t id,
    double value,
    int64_t ts)
{
    if(bufsize < TBP_FRAME_SIZE)
        return -1;
    write_le16(buf, id);
    union
    {
        double d;
        uint64_t u64;
    } cvt;

    cvt.d = value;
    write_le64((buf + 2), cvt.u64);
    write_le64((buf + 10), (uint64_t)ts);
    return TBP_FRAME_SIZE;
}

parse_result_t parse_tbp(
        const uint8_t *buf, 
        size_t len, 
        sensor_data_t *out, 
        size_t *consumed)
{
    if(buf == NULL || out == NULL || consumed == NULL)
        return PARSE_ERROR;
    if (len < TBP_FRAME_SIZE)
    { 
        if(consumed != NULL)
            *consumed = 0;
        return PARSE_NEED_MORE;
    }

    uint16_t id = read_le16(&buf[0]);

    union
    {
        uint64_t u64;
        double d;
    } cvt;

    cvt.u64 = read_le64(&buf[2]);

    int64_t ts =
        (int64_t)read_le64(&buf[10]);

    out->id = id;
    out->type = SENSOR_TEMPERATURE;
    out->value = cvt.d;
    out->ts= (time_t)ts;

    if(consumed != NULL)
        *consumed = TBP_FRAME_SIZE;

    return PARSE_OK;
}

ssize_t encode_hlp(
    uint8_t *buf, size_t bufsize,
    uint16_t id,
    double value,
    int64_t ts)
{
    if(bufsize < HLP_FRAME_SIZE)
        return -1;
    int len = snprintf(
        (char*)buf,
        HLP_FRAME_SIZE,
        "HUM %u %.1f %lld\n",
        id,
        value,
        (long long)ts);

    if (len < 0)
    {
        return 0;
    }

    return (size_t)len;
}

parse_result_t parse_hlp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed)
{
    if(buf == NULL || out == NULL || consumed == NULL)
        return PARSE_ERROR;
    const uint8_t *nl = memchr(buf, '\n', len);

    if (nl == NULL)
    {
        if (len >= HLP_FRAME_SIZE)
        {
            *consumed = 0;
            return PARSE_ERROR;
        }

        *consumed = 0;
        return PARSE_NEED_MORE;
    }

    size_t line_len = (size_t)(nl - buf) + 1;

    uint16_t id;
    double value;
    long long ts;

    int rc = sscanf(
        (const char *)buf,
        "HUM %hu %lf %lld",
        &id,
        &value,
        &ts);

    if (rc != 3)
    {
        *consumed = line_len;
        return PARSE_ERROR;
    }

    out->id = id;
    out->type = SENSOR_HUMIDITY;
    out->value = value;
    out->ts = (time_t)ts;

    *consumed = line_len;
    return PARSE_OK;
}

ssize_t encode_motion_tlv(
    uint8_t *buf, size_t bufsize,
    uint16_t id,
    uint32_t ts)
{
    if(bufsize < TLV_MOTION_FRAME_SIZE)
        return -1;
    buf[0] = 0x01;
    buf[1] = 0x06;

    write_be16(&buf[2], id);
    write_be32(&buf[4], ts);

    return TLV_MOTION_FRAME_SIZE;
}

ssize_t encode_door_tlv(
    uint8_t *buf, size_t bufsize,
    uint16_t id,
    uint8_t state,
    uint32_t ts)
{
    if(bufsize < TLV_DOOR_FRAME_SIZE)
        return -1;
    buf[0] = 0x02;
    buf[1] = 0x07;

    write_be16(&buf[2], id);

    buf[4] = state;

    write_be32(&buf[5], ts);

    return TLV_DOOR_FRAME_SIZE;
}

parse_result_t parse_evp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed)
{
    if(buf == NULL || out == NULL || consumed == NULL)
        return PARSE_ERROR;
    if (len < 2)
    {
        *consumed = 0;
        return PARSE_NEED_MORE;
    }

    uint8_t type = buf[0];
    uint8_t payload_len = buf[1];

    size_t frame_len = 2u + payload_len;

    if (len < frame_len)
    {
        *consumed = 0;
        return PARSE_NEED_MORE;
    }

    switch (type)
    {
    case 0x01: /* Motion */
    {
        if (payload_len != 6)
        {
            *consumed = frame_len;
            return PARSE_ERROR;
        }

        out->id = read_be16(&buf[2]);
        out->type = SENSOR_MOTION;
        out->value = 1.0;
        out->ts = (time_t)read_be32(&buf[4]);

        *consumed = frame_len;
        return PARSE_OK;
    }

    case 0x02: /* Door */
    {
        if (payload_len != 7)
        {
            *consumed = frame_len;
            return PARSE_ERROR;
        }

        out->id = read_be16(&buf[2]);
        out->type = SENSOR_DOOR;
        out->value = (double)buf[4];
        out->ts = (time_t)read_be32(&buf[5]);

        *consumed = frame_len;
        return PARSE_OK;
    }

    default:
        *consumed = frame_len;
        return PARSE_ERROR;
    }
}

ssize_t encode_sgp(
        uint8_t *buf,
        size_t bufsize,
        uint16_t id,
        uint16_t ppm,
        uint32_t ts)
{
    if(bufsize < SGP_FRAME_SIZE)
        return -1;
    buf[0] = SGP_MAGIC;

    write_be16(&buf[1], id);
    write_be16(&buf[3], ppm);
    write_be32(&buf[5], ts);

    uint16_t crc =
        crc16_ccitt_false(&buf[1], 8);

    write_be16(&buf[9], crc);

    return SGP_FRAME_SIZE;
}

int sgp_decode(
    const uint8_t *buf,
    size_t length,
    sgp_data_t *out)
{
    if (length != SGP_FRAME_SIZE)
    {
        return -1;
    }

    if (buf[0] != SGP_MAGIC)
    {
        return -1;
    }

    uint16_t rx_crc =
        read_be16(&buf[9]);

    uint16_t calc_crc =
        crc16_ccitt_false(&buf[1], 8);

    if (rx_crc != calc_crc)
    {
        return -2;
    }

    out->id  = read_be16(&buf[1]);
    out->ppm = read_be16(&buf[3]);
    out->ts  = read_be32(&buf[5]);

    return 0;
}


parse_result_t parse_sgp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed)
{
    if(buf == NULL || out == NULL || consumed == NULL)
        return PARSE_ERROR;
    if (len < SGP_FRAME_SIZE)
    {
        *consumed = 0;
        return PARSE_NEED_MORE;
    }

    if (buf[0] != SGP_MAGIC)
    {
        *consumed = 1;
        return PARSE_ERROR;
    }

    uint16_t rx_crc =
        read_be16(&buf[9]);

    uint16_t calc_crc =
        crc16_ccitt_false(&buf[1], 8);

    if (rx_crc != calc_crc)
    {
        *consumed = SGP_FRAME_SIZE;
        return PARSE_ERROR;
    }

    out->id = read_be16(&buf[1]);
    out->type = SENSOR_SMOKE_GAS;
    out->value = (double)read_be16(&buf[3]);
    out->ts = (time_t)read_be32(&buf[5]);

    *consumed = SGP_FRAME_SIZE;
    return PARSE_OK;
}

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF; // Initial value

    for (size_t i = 0; i < length; i++) {
        crc ^= ((uint16_t)data[i] << 8); // Move byte into MSB of 16-bit CRC
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021; // Polynomial 0x1021
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

ssize_t sgp_reply(parse_result_t result, uint8_t *reply, size_t cap)
{
    (void) result;
    (void) reply;
    (void) cap;
    return 0;

}


static void write_le16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void write_le64(uint8_t *buf, uint64_t value)
{
    for (int i = 0; i < 8; i++)
    {
        buf[i] = (uint8_t)((value >> (i * 8)) & 0xFF);
    }
}

static void write_be16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)((value >> 8) & 0xFF);
    buf[1] = (uint8_t)(value & 0xFF);
}

static void write_be32(uint8_t *buf, uint32_t value)
{
    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;
}

static uint16_t read_be16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static uint32_t read_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
        ((uint32_t)buf[1] << 16) |
        ((uint32_t)buf[2] << 8) |
        (uint32_t)buf[3];
}

static uint16_t read_le16(const uint8_t *buf)
{
    return ((uint16_t)buf[1] << 8) |
            (uint16_t)buf[0];
}

static uint64_t read_le64(const uint8_t *buf)
{
    uint64_t value = 0;

    for (int i = 0; i < 8; i++)
    {
        value |= ((uint64_t)buf[i]) << (i * 8);
    }

    return value;
}

const char* sensor_type_to_str(sensor_type_t type)
{
    switch(type){
        case SENSOR_TEMPERATURE: return "SENSOR_TEMPERATURE";
        case SENSOR_HUMIDITY: return "SENSOR_HUMIDITY";
        case SENSOR_MOTION: return "SENSOR_MOTION";
        case SENSOR_DOOR: return "SENSOR_DOOR";
        case SENSOR_SMOKE_GAS: return "SENSOR_SMOKE_GAS";
    }
    return "";

}
