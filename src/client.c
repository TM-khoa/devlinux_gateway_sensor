#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <getopt.h>
#include <time.h>
#include <stddef.h>
#include <stdbool.h>
#include "endecode_helper.h"
#include "common.h"
#include "sensor_config.h"
#include "sensor_types.h"

typedef struct
{
    const char *sensor_type;
    int sensor_id;

    const char *ip_address;
    uint16_t port;
    uint32_t interval_ms;
    bool glue;
    bool fragment;
    int corrupt;

} sensor_node_cfg_t;

typedef struct
{
    sensor_type_t type;
    sensor_data_t *data;
    size_t count;
    ssize_t (*encode)(
        uint8_t *buf,
        size_t bufsize,
        sensor_data_t *sensor);
    bool periodic;
} sensor_ctx_t;

int sennode_parse_args(int argc, char **argv, sensor_node_cfg_t *cfg);
void sennode_print_usage(const char *prog);
int sennode_tcp_open(const char* ip_address, uint16_t port);
int sennode_tcp_close(int client_fd);
void sleep_ms(int milliseconds);
ssize_t sennode_tcp_send(int client_fd, uint8_t* buf, size_t len);
ssize_t sennode_tcp_read(int client_fd, uint8_t* buf, size_t len);
static int get_sensor_ctx( const sensor_node_cfg_t *cfg, sensor_ctx_t *ctx);
static int send_fragment(
    int fd,
    uint8_t *buf,
    size_t bufsize,
    sensor_ctx_t *ctx,
    uint32_t interval_ms);

static int send_glue(
    int fd,
    uint8_t *buf,
    size_t bufsize,
    sensor_ctx_t *ctx);

static int send_normal(
    int fd,
    uint8_t *buf,
    size_t bufsize,
    sensor_ctx_t *ctx,
    uint32_t interval_ms);

void send_corrupt_data(
        int fd,
        uint8_t *buf,
        size_t bufsize,
        sensor_ctx_t *ctx);
static uint8_t tcpbuf[RX_BUF_SIZE * 2] = {0};
sensor_id_t sensor_id = 0;

static sensor_data_t temperature_sensor[] = {
    {11, SENSOR_TEMPERATURE, -32.31, 0},
    {11, SENSOR_TEMPERATURE, -23.91, 0},
    {11, SENSOR_TEMPERATURE, -11.88, 0},
    {11, SENSOR_TEMPERATURE, -5.83, 0},
    {11, SENSOR_TEMPERATURE, 0.83, 0},

    {11, SENSOR_TEMPERATURE, 30.88, 0},
    {11, SENSOR_TEMPERATURE, 25.25, 0},
    {11, SENSOR_TEMPERATURE, 21.37, 0},
    {11, SENSOR_TEMPERATURE, 18.82, 0},
    {11, SENSOR_TEMPERATURE, 41.50, 0},

    {11, SENSOR_TEMPERATURE, 51.51, 0},
    {11, SENSOR_TEMPERATURE, 65.35, 0},
    {11, SENSOR_TEMPERATURE, 43.57, 0},
    {11, SENSOR_TEMPERATURE, 70.10, 0},
    {11, SENSOR_TEMPERATURE, 41.38, 0},

    {11, SENSOR_TEMPERATURE, TEMP_MIN_C, 0},
    {11, SENSOR_TEMPERATURE, TEMP_MAX_C, 0},
    {11, SENSOR_TEMPERATURE, -100.10, 0},
    {11, SENSOR_TEMPERATURE, 101.22, 0},
};
static sensor_data_t humidity_sensor[] = {
    {22, SENSOR_HUMIDITY, 5.05, 0},
    {22, SENSOR_HUMIDITY, 10.87, 0},
    {22, SENSOR_HUMIDITY, 8.22, 0},
    {22, SENSOR_HUMIDITY, 9.87, 0},
    {22, SENSOR_HUMIDITY, 12.71, 0},

    {22, SENSOR_HUMIDITY, 35.24, 0},
    {22, SENSOR_HUMIDITY, 29.22, 0},
    {22, SENSOR_HUMIDITY, 57.48, 0},
    {22, SENSOR_HUMIDITY, 48.78, 0},
    {22, SENSOR_HUMIDITY, 68.30, 0},

    {22, SENSOR_HUMIDITY, 97.18, 0},
    {22, SENSOR_HUMIDITY, 74.24, 0},
    {22, SENSOR_HUMIDITY, 90.79, 0},
    {22, SENSOR_HUMIDITY, 86.10, 0},
    {22, SENSOR_HUMIDITY, 80.42, 0},

    {22, SENSOR_HUMIDITY, HUMIDITY_MIN, 0},
    {22, SENSOR_HUMIDITY, HUMIDITY_MAX, 0},
    {22, SENSOR_HUMIDITY, -1, 0},
    {22, SENSOR_HUMIDITY, 101, 0},
};
static sensor_data_t smoke_sensor[] = {
    {50, SENSOR_SMOKE_GAS, 50.28, 0},
    {50, SENSOR_SMOKE_GAS, 693.32, 0},
    {50, SENSOR_SMOKE_GAS, 135.85, 0},
    {50, SENSOR_SMOKE_GAS, 400.12, 0},
    {50, SENSOR_SMOKE_GAS, 539.20, 0},
    {50, SENSOR_SMOKE_GAS, SMOKE_PPM_MIN, 0},
    {50, SENSOR_SMOKE_GAS, SMOKE_PPM_MAX, 0},
};
static sensor_data_t motion_sensor[] = {
    {30, SENSOR_MOTION, 0, 0},
    {30, SENSOR_MOTION, 0, 0},
    {30, SENSOR_MOTION, 0, 0},
    {30, SENSOR_MOTION, 0, 0},
    {30, SENSOR_MOTION, 0, 0},

};

static sensor_data_t door_sensor[] = {
    {40, SENSOR_DOOR, DOOR_OPEN, 0},
    {40, SENSOR_DOOR, DOOR_CLOSE, 0},
    {40, SENSOR_DOOR, DOOR_OPEN, 0},
    {40, SENSOR_DOOR, 2, 0},
    {40, SENSOR_DOOR, -1, 0},
};

double random_range(double min, double max)
{
    /* rand() is adequate for deterministic test data, not security use. */
    return min + ((double)rand() / (double)RAND_MAX) * (max - min);
}
int client_fd = 0;

static int wait_sgp_reply(int fd)
{
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    struct timeval tv;

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int rc = select(fd + 1, &rfds, NULL, NULL, &tv);

    if (rc <= 0)
        return 0;

    uint8_t rsp;

    ssize_t n = recv(fd, &rsp, 1, 0);

    if (n != 1)
        return 0;

    printf("Receive 0x%x\n", rsp);
    if (rsp == SGP_ACK)
        return 1;

    return -1;
}

static int send_sgp_with_retry( int fd, const uint8_t *frame, size_t len)
{
    int retry;

    for (retry = 0; retry < SGP_MAX_RETRY; retry++) {
        ssize_t sent = send(fd, frame, len, 0);

        if (sent != (ssize_t)len)
            continue;

        int reply = wait_sgp_reply(fd);

        if (reply == 1)
        {
            return 0;
        }

        /*
         * timeout or NAK
         * resend
         */
    }

    return -1;
}

int main(int argc, char **argv)
{
    sensor_node_cfg_t cfg;
    sensor_ctx_t ctx;
    srand((unsigned int)time(NULL));

    if (sennode_parse_args(argc, argv, &cfg) != 0) {
        sennode_print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    printf("sensor_type : %s\n", cfg.sensor_type);
    printf("sensor_id: %d\n"   , cfg.sensor_id);
    printf("ip_address  : %s\n", cfg.ip_address);
    printf("port        : %u\n", cfg.port);
    printf("interval_ms : %u\n", cfg.interval_ms);
    printf("glue        : %s\n", cfg.glue ? "yes" : "no");
    printf("fragment    : %s\n", cfg.fragment ? "yes" : "no");

    printf("corrupt     : %s\n",
           cfg.corrupt ? "yes" : "no");
    client_fd = sennode_tcp_open(cfg.ip_address, cfg.port);

    if(get_sensor_ctx(&cfg, &ctx) != 0)
    {
        printf("Unsupported sensor\n");
        return -1;
    }

    if(cfg.glue) {
        printf("Send glue\n");
        if(send_glue(client_fd, tcpbuf, sizeof(tcpbuf), &ctx) != 0) {
            fprintf(stderr, "Error: Fail send_glue");
        }
    }
    else if(cfg.fragment) {
        printf("Send fraqment\n");
        if(send_fragment(client_fd, tcpbuf, sizeof(tcpbuf), &ctx, cfg.interval_ms) != 0) {
            fprintf(stderr, "Error: Fail send_fragment");
        }
    }
    else if (cfg.corrupt) {
        send_corrupt_data(client_fd, tcpbuf, sizeof(tcpbuf), &ctx);
    }
    else {
        printf("Send normal\n");
        if(send_normal(client_fd, tcpbuf, sizeof(tcpbuf), &ctx, cfg.interval_ms) != 0){
            fprintf(stderr, "Error: Fail send_normal");
        }
    }

    int err = sennode_tcp_close(client_fd);
    if(err == -1) {
        printf("Error: Fail to close socket\n");
        return -1;
    }
    return EXIT_SUCCESS;
}

void sennode_print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s <sensor-type> <sensor-id> <ip-address> <port> <interval-ms> [options]\n\n",
           prog);

    printf("Options:\n");
    printf("  --fragment    Split frame and send chunks\n");
    printf("  --glue        Merge multiple frames before sending\n");
    printf("  --corrupt     Inject malformed frames\n");
}


int sennode_tcp_open(const char* ip_address, uint16_t port) {
    int client_fd;
    struct sockaddr_in serv_addr;

    /* Create socket */
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    /* Convert IPv4 text address to binary form */
    if (inet_pton(AF_INET, ip_address, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    /* Connect to the server */
    if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }
    return client_fd;
}
int sennode_tcp_close(int client_fd) { return close(client_fd); }
ssize_t sennode_tcp_send(int client_fd, uint8_t* buf, size_t len) { return send(client_fd, buf, len, 0); }
ssize_t sennode_tcp_read(int client_fd, uint8_t* buf, size_t len)
{
    ssize_t valread = recv(client_fd, buf, len - 1, 0);
    if (valread > 0) { buf[valread] = '\0'; }
    return valread;
}


int sennode_parse_args(int argc,
                      char **argv,
                      sensor_node_cfg_t *cfg)
{
    static struct option long_options[] =
    {
        {"fragment", no_argument, 0, 'f'},
        {"glue",     no_argument, 0, 'g'},
        {"corrupt",  no_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    memset(cfg, 0, sizeof(*cfg));

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc,
                              argv,
                              "",
                              long_options,
                              &option_index)) != -1)
    {
        switch (opt)
        {
            case 'f':
                cfg->fragment = 1;
                break;

            case 'g':
                cfg->glue = 1;
                break;

            case 'c':
                cfg->corrupt = 1;
                break;

            default:
                return -1;
        }
    }

    int remaining = argc - optind;

    if (remaining != 5)
    {
        fprintf(stderr,
                "Error: Missing required arguments.\n");
        return -1;
    }

    cfg->sensor_type = argv[optind + 0];
    cfg->sensor_id = atoi(argv[optind + 1]);
    cfg->ip_address = argv[optind + 2];
    cfg->port = (uint16_t)strtoul(argv[optind + 3], NULL, 10);
    cfg->interval_ms = (uint32_t)strtoul(argv[optind + 4], NULL, 10);

    return 0;
}

void sleep_ms(int milliseconds)
{
    struct timespec ts;

    /* Calculate whole seconds */
    ts.tv_sec = milliseconds / 1000;

    /* Convert remaining milliseconds to nanoseconds */
    ts.tv_nsec = (milliseconds % 1000) * 1000000L;

    /* Loop in case nanosleep is interrupted by a signal */
    while (nanosleep(&ts, &ts) == -1);
}

static int send_normal(
    int fd,
    uint8_t *buf,
    size_t bufsize,
    sensor_ctx_t *ctx,
    uint32_t interval_ms)
{

    for(size_t i = 0; i < ctx->count; i++)
    {
        time(&ctx->data[i].ts);

        ssize_t len =
            ctx->encode(
                buf,
                bufsize,
                &ctx->data[i]);

        if(len < 0)
            return -1;

        if(SENSOR_SMOKE_GAS == ctx->type) {
            if(send_sgp_with_retry(fd, buf, len) == -1 ) {
                fprintf(stderr, "SGP transmit failed after 3 retries\n");
            }
        }
        else
            send(fd, buf, len, 0);

        if(ctx->periodic)
        {
            sleep_ms(interval_ms);
        }
        else {
            uint16_t sleep_random = (rand() % 10);
            sleep_random = sleep_random == 0 ? 1 : sleep_random; // Avoid sleep_random = 0
            printf("sleep_random:%u second\n", sleep_random);
            sleep_ms(sleep_random * 1000);
        }
    }
    memset(buf, 0, bufsize);

    return 0;
}
static int send_glue(
    int fd,
    uint8_t *buf,
    size_t bufsize,
    sensor_ctx_t *ctx)
{
    size_t total_len = 0;

    for(size_t i = 0; i < ctx->count; i++)
    {
        time(&ctx->data[i].ts);

        ssize_t len =
            ctx->encode(
                buf + total_len,
                bufsize - total_len,
                &ctx->data[i]);

        if(len < 0)
            return -1;

        total_len += len;
    }

    for(unsigned long i = 0; i < total_len; i++) {
        printf("0x%x ", buf[i]);
    }
    printf("\n");
    if(SENSOR_SMOKE_GAS == ctx->type) {
        if(send_sgp_with_retry(fd, buf, total_len) == -1 ) {
            fprintf(stderr, "SGP transmit failed after 3 retries\n");
        }
    }
    else 
        send(fd, buf, total_len, 0);
    return 0;
}
static int send_fragment(
    int fd,
    uint8_t *buf,
    size_t bufsize,
    sensor_ctx_t *ctx,
    uint32_t interval_ms)
{

    for(size_t i = 0; i < ctx->count; i++)
    {
        time(&ctx->data[i].ts);

        ssize_t len =
            ctx->encode(
                buf,
                bufsize,
                &ctx->data[i]);

        if(len < 0)
            return -1;

        size_t offset = 0;

        while(offset < (size_t)len)
        {
            size_t chunk = 5;

            if(chunk > len - offset)
            {
                chunk = len - offset;
            }

            if(SENSOR_SMOKE_GAS == ctx->type) {
                if(send_sgp_with_retry(fd, buf + offset, chunk) == -1 ) {
                    fprintf(stderr, "SGP transmit failed after 3 retries\n");
                }
            }
            else
                send( fd, buf + offset, chunk, 0);

            offset += chunk;

            sleep_ms(interval_ms);
        }
    }

    memset(buf, 0, bufsize);
    return 0;
}

static ssize_t encode_temperature( uint8_t *buf, size_t size, sensor_data_t *sensor)
{
    return encode_tbp( buf, size, sensor->id, sensor->value, sensor->ts);
}

static ssize_t encode_humidity( uint8_t *buf, size_t size, sensor_data_t *sensor)
{
    return encode_hlp( buf, size, sensor->id, sensor->value, sensor->ts);
}

static ssize_t encode_motion( uint8_t *buf, size_t size, sensor_data_t *sensor)
{
    return encode_motion_tlv( buf, size, sensor->id, sensor->ts);
}

static ssize_t encode_door( uint8_t *buf, size_t size, sensor_data_t *sensor)
{
    return encode_door_tlv( buf, size, sensor->id, (uint8_t)sensor->value, sensor->ts);
}

static ssize_t encode_smoke( uint8_t *buf, size_t size, sensor_data_t *sensor)
{
    return encode_sgp( buf, size, sensor->id, sensor->value, sensor->ts);
}

static int get_sensor_ctx(
    const sensor_node_cfg_t *cfg,
    sensor_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    typedef struct {
        sensor_ctx_t ctx;
        const char *sensor_type;
    } st_ctx_type_t;
    st_ctx_type_t ctxtype[] = {
        {
            {SENSOR_TEMPERATURE, temperature_sensor, TOTAL_ITEMS(temperature_sensor), encode_temperature, true},
            "temperature"
        },
        {
            {SENSOR_HUMIDITY, humidity_sensor, TOTAL_ITEMS(humidity_sensor), encode_humidity, true}, 
            "humidity"
        },
        {
            {SENSOR_MOTION, motion_sensor, TOTAL_ITEMS(motion_sensor), encode_motion, false},
            "motion"
        },
        {
            {SENSOR_DOOR, door_sensor, TOTAL_ITEMS(door_sensor), encode_door, false},
            "door"
        },
        {
            {SENSOR_SMOKE_GAS, smoke_sensor, TOTAL_ITEMS(smoke_sensor), encode_smoke, false},
            "smoke_gas"
        }
    };
    for (unsigned long i = 0; i < TOTAL_ITEMS(ctxtype); i++) {
        if(strcmp(cfg->sensor_type, ctxtype[i].sensor_type) == 0) {
            *ctx = ctxtype[i].ctx;
            printf("Sensor type:%s, items:%lu\n", ctxtype[i].sensor_type, ctx->count);
            for(unsigned long i = 0; i < ctx->count; i++) {
                ctx->data[i].id = cfg->sensor_id;
            }
            ctx->data[2].id = 1000; // Mix an unavailable ID
            return 0;
        }
    }
    return -1;

}


void send_corrupt_data(
        int fd,
        uint8_t *buf,
        size_t bufsize,
        sensor_ctx_t *ctx)
{
    ssize_t len;
    if (SENSOR_HUMIDITY == ctx->type) {
        len = encode_humidity(buf, bufsize, &humidity_sensor[0]);
        memcpy(buf, "abc", sizeof("abc")); // corrupt HUM character
        send(fd, buf, len, 0);
        sleep_ms(1000);
        len = encode_humidity(buf, bufsize, &humidity_sensor[1]);
        buf[len - 1] = 'Z'; // Remove \n
        const char *dummy_text = "This is dummy text that make humidity protocol is oversize of 64 bytes data";
        size_t dummy_text_len = strlen(dummy_text);
        memcpy(buf + len, dummy_text, dummy_text_len); // Oversize HLP
        send(fd, buf, len + dummy_text_len, 0);
        sleep_ms(1000);
    }
    else if (SENSOR_MOTION == ctx->type) {
        len = encode_motion(buf, bufsize, &motion_sensor[0]);
        buf[0] = 0x03; // Malform first frame
        send(fd, buf, len, 0);
        sleep_ms(1000);
        len = encode_motion(buf, bufsize, &motion_sensor[1]);
        buf[1] = 0x05; // Bad length 
        send(fd, buf, len, 0);
        sleep_ms(1000);
    }
    else if (SENSOR_DOOR == ctx->type) {
        len = encode_door(buf, bufsize, &door_sensor[0]);
        buf[0] = 0x03; // Malform first frame
        send(fd, buf, len, 0);
        sleep_ms(1000);
        len = encode_door(buf, bufsize, &door_sensor[1]);
        buf[1] = 0x05; // Bad length 
        send(fd, buf, len, 0);
        sleep_ms(1000);
    }
    else if (SENSOR_SMOKE_GAS == ctx->type) {
        len = encode_smoke(buf, bufsize, &smoke_sensor[0]);
        buf[0] = 0x12; // Bad magic
        if (send_sgp_with_retry(fd, buf, len) != 0) {
            fprintf(stderr, "SGP transmit failed after 3 retries\n");
        }
        send(fd, buf, len, 0);
        sleep_ms(1000);
        len = encode_smoke(buf, bufsize, &smoke_sensor[1]);
        buf[len - 1] = 0x00; // Bad CRC
        if (send_sgp_with_retry(fd, buf, len) != 0) {
            fprintf(stderr, "SGP transmit failed after 3 retries\n");
        }
        send(fd, buf, len, 0);
        sleep_ms(1000);
    }

}
