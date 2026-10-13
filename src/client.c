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

typedef struct
{
    const char *sensor_type;
    int sensor_id;

    const char *ip_address;
    uint16_t port;
    uint32_t interval_ms;
    bool is_glue; // 1 = glue, 0 = fragment
    int corrupt;

} sensor_node_cfg_t;

int sennode_parse_args(int argc, char **argv, sensor_node_cfg_t *cfg);
void sennode_print_usage(const char *prog);
int sennode_tcp_open(const char* ip_address, uint16_t port);
int sennode_tcp_close(int client_fd);
void sleep_ms(int milliseconds);
ssize_t sennode_tcp_send(int client_fd, uint8_t* buf, size_t len);
ssize_t sennode_tcp_read(int client_fd, uint8_t* buf, size_t len);
uint8_t tcpbuf[RX_BUF_SIZE] = {0};

// sensor_data_t temp1_sample = { 10, SENSOR_TEMPERATURE, 21.5, 0 };
// sensor_data_t temp2_sample = { 11, SENSOR_TEMPERATURE, 30.56, 0 };
// sensor_data_t humid1_sample = { 21, SENSOR_HUMIDITY, 23.24, 0 };
// sensor_data_t humid2_sample = { 22, SENSOR_HUMIDITY, 90.88, 0 };
// sensor_data_t montion_sample = { 30, SENSOR_MOTION, 0, 0 };
// sensor_data_t dooropen_sample = { 40, SENSOR_DOOR, 1, 0 };
// sensor_data_t doorclose_sample = { 40, SENSOR_DOOR, 0, 0 };
// sgp_data_t smokegas_sample = { 50, SENSOR_SMOKE_GAS, 100.0, 0 };

float temp10[] = {-11.88, -5.83, 0.83, 3.88, 10.23, 14.85, 12.88, 18.77, 19.87};
float temp11[] = {22.88, 35.83, 4.63, 31.14, 19.40, 24.71, 32.08, 28.80, 29.82};
float humid21[] = {95.87, 50.22, 28.87};
float humid22[] = {10.11, 70.18, 63.42};

int client_fd = 0;

int main(int argc, char **argv)
{
    sensor_node_cfg_t cfg;

    if (sennode_parse_args(argc, argv, &cfg) != 0)
    {
        sennode_print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    printf("sensor_type : %s\n", cfg.sensor_type);
    printf("sensor_id: %d\n"   , cfg.sensor_id);
    printf("ip_address  : %s\n", cfg.ip_address);
    printf("port        : %u\n", cfg.port);
    printf("interval_ms : %u\n", cfg.interval_ms);
    printf("is glue     : %s\n",
           cfg.is_glue ? "yes" : "no");

    printf("corrupt     : %s\n",
           cfg.corrupt ? "yes" : "no");
    client_fd = sennode_tcp_open(cfg.ip_address, cfg.port);
    time_t ts;
    ssize_t len = 0;
    time(&ts);

    if(strcmp(cfg.sensor_type, "temperature") == 0)
    {
        len = encode_tbp(tcpbuf, sizeof(tcpbuf), cfg.sensor_id, temp10[0], ts);
        if(len == -1){
            printf("Error: framesize smaller than expected\n");
            return -1;
        }
    }
    else if ((strcmp(cfg.sensor_type, "humidity") == 0)) {
        len = encode_hlp(tcpbuf, sizeof(tcpbuf), cfg.sensor_id, humid21[0], ts);
        if(len == -1){
            printf("Error: framesize smaller than expected\n");
            return -1;
        }
        tcpbuf[len] = '\0';
        printf("%s", tcpbuf);

    }
    else if ((strcmp(cfg.sensor_type, "motion") == 0)) {
        len = encode_motion_tlv(tcpbuf, sizeof(tcpbuf), cfg.sensor_id, ts);
        if(len == -1){
            printf("Error: framesize smaller than expected\n");
            return -1;
        }
    }
    else if ((strcmp(cfg.sensor_type, "door") == 0)) {
        len = encode_door_tlv(tcpbuf, sizeof(tcpbuf), cfg.sensor_id, DOOR_OPEN, ts);
        if(len == -1){
            printf("Error: framesize smaller than expected\n");
            return -1;
        }

    }
    else if ((strcmp(cfg.sensor_type, "smoke_gas") == 0)) {
        len = encode_sgp(tcpbuf, sizeof(tcpbuf), cfg.sensor_id, 100.0, ts);
        if(len == -1){
            printf("Error: framesize smaller than expected\n");
            return -1;
        }

    }
    else {
        printf("Error: Not found sensor type\n");
    }

    len = sennode_tcp_send(client_fd, tcpbuf, len);
    if(len == -1){
        printf("Error: Fail to send via TCP\n");
        return -1;
    }

    // for(unsigned long i = 0; i < sizeof(temp10)/sizeof(temp10[0]); i++) {
    //     printf("Send %.2f\n", temp10[i]);
    //     ssize_t len = encode_tbp(tcpbuf, sizeof(tcpbuf), cfg.sensor_id, temp10[i], ts);
    //     if(len == -1){
    //         printf("Error: framesize smaller than expected\n");
    //         return -1;
    //     }
    //     len = sennode_tcp_send(client_fd, tcpbuf, len);
    //     if(len == -1){
    //         printf("Error: Fail to send via TCP\n");
    //         return -1;
    //     }
    //     sleep_ms(cfg.interval_ms);
    // }

    // for(int i = 0; i < len; i++) {
    //     printf("0x%x ", tcpbuf[i]);
    //
    // }
    // printf("\n");

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
                cfg->is_glue = 0;
                break;

            case 'g':
                cfg->is_glue = 1;
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

