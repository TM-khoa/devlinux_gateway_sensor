#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <poll.h>
#include "protocol_adapter.h"
#include "connection_manager.h"
#include "endecode_helper.h"
#include "logger.h"
#include "gateway.h"


#ifdef LOG_CONNMGR
static const char *TAG = "CONN";
#else
static const char* TAG="";
#endif

size_t sgp_reply(parse_result_t result, uint8_t *reply, size_t cap);

static connmgr_t *p_connmgr;
static sbuffer_t *p_sbuf;

static fd_set read_fds;
static int max_fd = 0;
static bool stop_handle_conn = 0;
static uint16_t base_port;
extern int g_connmgr_stoppipefd[2];

static const protocol_adapter_t atbp = {"TBP", 0, parse_tbp, NULL};
static const protocol_adapter_t ahlp = {"HLP", 1, parse_hlp, NULL};
static const protocol_adapter_t aevp = {"EVP", 2, parse_evp, NULL};
static const protocol_adapter_t asgp = {"SGP", 3, parse_sgp, 
    .build_reply = sgp_reply, };

static int register_new_connection_adapter(connection_t *conn, uint16_t port)
{
    uint16_t port_offset = port - base_port;
    const protocol_adapter_t *p_adapter[] ={
        &atbp, &ahlp, &aevp, &asgp,
    };
    uint8_t no_adapter = sizeof(p_adapter)/sizeof(p_adapter[0]);
    for(uint8_t a = 0; a < no_adapter; a++) {
        if(port_offset == p_adapter[a]->port_offset) {
            conn->adapter = p_adapter[a];
            LDB("Found adapter of:%s", conn->adapter->name);
            return 0;
        }
    }
    printf("Error: Not found adapter for port:%u", port);
    return 1;

}


int handle_connection()
{
    printf("Waiting new connection\n");
    char s[100] = {0};
    while (stop_handle_conn == false) {
        FD_ZERO(&read_fds);
        FD_SET(g_connmgr_stoppipefd[0], &read_fds);
        for (int i = 0; i < NUM_PORTS; i++) {
            int listen_fd = p_connmgr->listen_fd[i];
            FD_SET(listen_fd, &read_fds);
        }

        /* Block until one of the ports receives an incoming connection */
        select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if(stop_handle_conn == true)
            break;

        /* Check which server port socket is listening and accept connection */
        for (int i = 0; i < NUM_PORTS; i++) {
            int listen_fd = p_connmgr->listen_fd[i];
            if (FD_ISSET(listen_fd, &read_fds)) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
                struct sockaddr_in local_addr;
                socklen_t len;

                len = sizeof(local_addr);
                getsockname(conn_fd, (struct sockaddr *)&local_addr, &len);
                int local_port = ntohs(local_addr.sin_port);

                // struct sockaddr_in peer_addr;
                // len = sizeof(peer_addr);
                // getpeername(conn_fd, (struct sockaddr *)&peer_addr, &len);
                // printf("peer  port = %u\n", ntohs(peer_addr.sin_port));

                int client_port = ntohs(client_addr.sin_port);
                /* Should use local port instead official port when testing localhost */
                client_port = local_port;
                char *client_ip = inet_ntoa(client_addr.sin_addr);
                LDB("%s:Client connected from IP: %s, Port: %d", client_ip, client_port);
                if(conn_fd < 0) {
                    snprintf(s,sizeof(s), "Connection attempt failed on %u port, reason:accept error", client_port);
                    ipc_write_logprocess(s);
                    perror("Accept failed");
                    exit(EXIT_FAILURE);
                }
                /* TODO: Dispatch in another thread to handle buffer  */

                /* Checking whether new connection has already existed */
                size_t n_conns = p_connmgr->n_conns;
                bool conn_existed = false;
                connection_t conn;
                sensor_data_t sensor;
                for(unsigned long c = 0; c < n_conns; c++) {
                    conn = p_connmgr->conns[c];
                    /* If found existed client connection */
                    if(conn.fd == conn_fd){
                        conn_existed = true;
                        LDB("Found existed connection");
                        break;
                    }
                }

                /* Paste incoming data to connection rx buffer */
                ssize_t valread = recv(conn_fd, conn.rx, RX_BUF_SIZE - 1, 0);
                if (valread < 0) {
                    printf("Error: Fail to get data from client\n");
                }
                conn.rx_len = (size_t)valread;
                conn.rx[valread] = '\0'; // Add NULL-terminate string
                LDB("valread %ld", valread);

                /* Register new client connection to reuse */
                if(conn_existed == false) {
                    printf("New connection arrived\n");
                    conn.fd = conn_fd;
                    if(register_new_connection_adapter(&conn, client_port) != 0) {
                        printf("Error: Not found adapter for port:%u\n", client_port);
                        if (close(conn.fd) == -1) {
                            perror("Error closing file descriptor");
                            return 1;
                        }
                        break;
                    }
                    parse_result_t err = conn.adapter->parse(conn.rx, conn.rx_len, &sensor, &conn.rx_len);
                    if(err == PARSE_ERROR) {
                        printf("Error: fail to parse, check whether input pointer is NULL\n");
                        if (close(conn.fd) == -1) {
                            perror("Error closing file descriptor");
                            return 1;
                        }
                        continue;
                    }
                    snprintf(s,sizeof(s), "A sensor node with %u has opened a new connection\n", sensor.id);
                    ipc_write_logprocess(s);
                    conn.identified = true;
                    conn.sensor_id = sensor.id;
                    /* Add new connection to connection manager */
                    p_connmgr->conns[n_conns] = conn;
                    LDB("n_conns:%lu", n_conns);
                    p_connmgr->n_conns++;
                }
                /* If existed client connection */
                else {
                    parse_result_t err = conn.adapter->parse(conn.rx, conn.rx_len, &sensor, NULL);
                    if(err == PARSE_ERROR) {
                        printf("Error: fail to parse, err:%d", err);
                        continue;
                    }
                }
                LDB("Sensor ID:%u", sensor.id);
                LDB("Sensor type:%s", sensor_type_to_str(sensor.type));
                LDB("Sensor value:%.2f", sensor.value);
                LDB("Timestamp:%ld", sensor.ts);
                LDB("Put sensor data to sbuffer");
                sbuffer_insert(p_sbuf, &sensor);
            }
        }
    }
    /* Close client connection */
    LDB("Release resource client connection");
    size_t n_conns = p_connmgr->n_conns;
    for(unsigned long c = 0; c < n_conns; c++) {
        connection_t conn = p_connmgr->conns[c];
        if (conn.fd != -1 && close(conn.fd) == -1) {
            perror("Error closing file descriptor");
            return 1;
        }
        conn.fd = -1;
        p_connmgr->conns[c] = conn;
    }
    /* Close listening port on server */
    LDB("Release resource listen port");
    for (int p = 0; p < NUM_PORTS; p++) {
        int listen_fd = p_connmgr->listen_fd[p];
        if (listen_fd != -1 && close(listen_fd) == -1) {
            perror("Error closing file descriptor");
            return 1;
        }
        p_connmgr->listen_fd[p] = -1;
        p_connmgr->n_conns = 0;
    }
    return 0;

}

void connmgr_stop()
{
    stop_handle_conn = true;
}

void connmgr_start(connmgr_t *connmgr, uint16_t baseport, sbuffer_t *sbuf)
{
    p_connmgr = connmgr;
    p_sbuf = sbuf;
    p_connmgr->n_conns = 0;
    stop_handle_conn = false;
    base_port = baseport;
    int listen_fd;

    connection_t conn;
    conn.fd = -1; // To prevent invalid access
    for (int i = 0; i < MAX_CONN; i++) {
        p_connmgr->conns[i]= conn;
    }

    /* Add signal handler pipe to listen list */
    FD_SET(g_connmgr_stoppipefd[0], &read_fds);
    max_fd = g_connmgr_stoppipefd[0];

    for (int i = 0; i < NUM_PORTS; i++) {
        LDB("Listening on port:%u", baseport + i);
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(baseport + i);

        /* Bind the socket to the port */
        if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }

        listen(listen_fd, SOMAXCONN);

        if (listen_fd > max_fd) 
            max_fd = listen_fd;
        p_connmgr->listen_fd[i] = listen_fd;
    }
    handle_connection();
}
