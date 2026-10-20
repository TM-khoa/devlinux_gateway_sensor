#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <poll.h>
#include "common.h"
#include "protocol_adapter.h"
#include "connection_manager.h"
#include "endecode_helper.h"
#include "logger.h"

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
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

static pthread_mutex_t conn_mutex = PTHREAD_MUTEX_INITIALIZER;
static const protocol_adapter_t atbp = {"TBP", 0, parse_tbp, NULL};
static const protocol_adapter_t ahlp = {"HLP", 1, parse_hlp, NULL};
static const protocol_adapter_t aevp = {"EVP", 2, parse_evp, NULL};
static const protocol_adapter_t asgp = {"SGP", 3, parse_sgp, 
    .build_reply = sgp_reply, };

static void reset_connection()
{
    LDB("Reset value of connection slots");
    for (size_t i = 0; i < MAX_CONN; ++i) {
        p_connmgr->conns[i].fd = -1;
        p_connmgr->conns[i].adapter = NULL;
        p_connmgr->conns[i].rx_len = 0;
    }
}

static int conn_get_adapter(connection_t *conn, uint16_t port)
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
    return 1;

}

static connection_t *claim_connection_slot(
    int fd)
{
    connection_t *slot = NULL;
    size_t n_conns = p_connmgr->n_conns++;

    pthread_mutex_lock(&conn_mutex);
    if(n_conns >= MAX_CONN)
        return slot;

    for (size_t i = 0; i < MAX_CONN; ++i) {
        if (p_connmgr->conns[i].fd == -1) {
            slot = &p_connmgr->conns[i];
            slot->fd = fd;
            slot->rx_len = 0;
            LDB("Found empty conn slot:%ld, n_conns:%lu", i, n_conns);
            break;
        }
    }
    n_conns++;
    p_connmgr->n_conns = n_conns;
    pthread_mutex_unlock(&conn_mutex);

    return slot;

}

int process_received_data(connection_t *conn, uint8_t *recvbuf, size_t len)
{
    sensor_data_t sensor;
    size_t consumed = 0;
    uint8_t *rxbuf = conn->rx;
    size_t rxlen = conn->rx_len;
    size_t total_consumed = 0;

    /* Append the uncompleted frame if any */
    memcpy(rxbuf + rxlen, recvbuf, len);
    rxlen += len;
    while (rxlen > 0) {
        parse_result_t err = conn->adapter->parse(rxbuf + total_consumed, rxlen, &sensor, &consumed);
        if(PARSE_ERROR == err) {
            fprintf(stderr,"PARSE_ERROR: Frame error\n");
            /* Move remaining bytes to the beginning of buffer */
            memset(rxbuf + rxlen, 0, len); // Cleanup malformed frame
            rxlen -= len; // Revert back to previous
            break;
        }
        else if (PARSE_OK == err) {
            if(rxlen < consumed)
                LDB("rxlen is smaller than consumed, can't substract");
            else {
                rxlen -= consumed;
                total_consumed += consumed;
            }
            LDB("PARSE_OK, total_consumed:%lu, consumed:%lu, rxlen:%lu", total_consumed, consumed, rxlen);
            if(conn->identified == false) { ipc_log_event("A sensor node with %u has opened a new connection", sensor.id);
                conn->identified = true;
            }
            if(conn->sensor_id != sensor.id) {
                LDB("Error: Sensor ID %u mismatch with previous packet has ID %u", sensor.id, conn->sensor_id);
            }
            conn->sensor_id = sensor.id;
            LDB("Sensor ID:%u, type:%s, value:%.2f, Timestamp:%ld", 
                    sensor.id, sensor_type_to_str(sensor.type), sensor.value, sensor.ts);
            // if(sbuffer_insert(p_sbuf, &sensor) != 0){
            //     fprintf(stderr,"Fail to insert share buffer\n");
            // }
        }
        else if (PARSE_NEED_MORE == err) {
            LDB("PARSE_NEED_MORE");
            /* Move remaining bytes to the beginning of buffer */
            memcpy(rxbuf, rxbuf + total_consumed, rxlen);
            break;
        }
    };
    LDB("---------------------------------------------------------");
    conn->rx_len = rxlen;
    return 0;
}


static void *thread_conn_worker(void *arg)
{
    connection_t *conn = (connection_t *)arg;
    const int fd = conn->fd;
    uint8_t recvbuf[100] = {0};
    LDB("Worker: Created");

    for (;;) {
        ssize_t n = recv(fd, recvbuf, sizeof(recvbuf), 0);

        if (n > 0) {
            LDB("Worker: Receive %lu", n);
            process_received_data(conn, recvbuf, n);
            continue;
        }

        if (n == 0) {
            /* Client performed an orderly TCP shutdown. */
            LDB("Worker: Client disconnect");
            if(conn->identified) {
                ipc_log_event("A sensor node with %u has closed the connection", conn->sensor_id);
                conn->identified = false;
            }
            break;
        }

        if (errno == EINTR) {
            /* Interrupted by a signal; retry. */
            continue;
        }

        /* Other receive error. */
        perror("recv");
        break;
    }

    close(fd);

    /*
     * Mark the slot free only after this worker has finished
     * using the connection. The main thread must use the same
     * mutex when searching for free slots.
     */
    pthread_mutex_lock(&conn_mutex);
    conn->fd = -1;
    conn->rx_len = 0;
    conn->adapter = NULL;
    pthread_mutex_unlock(&conn_mutex);
    p_connmgr->n_conns--;
    LDB("Worker: Free conn slot");

    return NULL;
}

void conn_accept_and_dispatch()
{
    LDB("Waiting new connection");
    while (stop_handle_conn == false) {
        FD_ZERO(&read_fds);
        FD_SET(g_connmgr_stoppipefd[0], &read_fds);
        for (int i = 0; i < NUM_PORTS; i++) {
            int listen_fd = p_connmgr->listen_fd[i];
            FD_SET(listen_fd, &read_fds);
        }

        /* Block until one of the ports receives an incoming connection */
        LDB("Waiting at select()");
        select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if(stop_handle_conn == true)
            break;

        /* Check which server port socket is listening and accept connection */
        for (int i = 0; i < NUM_PORTS; i++) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int listen_fd = p_connmgr->listen_fd[i];
            if (FD_ISSET(listen_fd, &read_fds)) {
                int conn_fd = accept(
                        listen_fd,
                        (struct sockaddr *)&client_addr,
                        &client_len
                        );

                if (conn_fd < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    ipc_log_event("Connection attempt failed on %u port (accept error)", base_port + i);

                    perror("accept");
                    continue;
                }
                LDB("accept OK");

                /* Determine the server-side local port. */
                struct sockaddr_in local_addr;
                socklen_t local_len = sizeof(local_addr);

                if (getsockname(conn_fd,
                            (struct sockaddr *)&local_addr,
                            &local_len) < 0) {
                    perror("getsockname");
                    close(conn_fd);
                    continue;
                }

                int server_port = ntohs(local_addr.sin_port);
                int peer_port = ntohs(client_addr.sin_port);

                char client_ip[INET_ADDRSTRLEN];

                if (inet_ntop(AF_INET,
                            &client_addr.sin_addr,
                            client_ip,
                            sizeof(client_ip)) == NULL) {
                    perror("inet_ntop");
                    close(conn_fd);
                    continue;
                }

                LDB("Client %s:%d connected to server port %d",
                        client_ip, peer_port, server_port);

                connection_t *slot = claim_connection_slot(conn_fd);
                if(conn_get_adapter(slot, server_port) != 0) {
                    fprintf(stderr, "Fail to get adapter for port:%u\n", server_port);
                    close(conn_fd);
                    continue;
                }

                if (slot == NULL) {
                    fprintf(stderr, "Maximum connection slots reached\n");
                    ipc_log_event("Connection attempt failed on %u port (too many connections)", base_port + i);
                    close(conn_fd);
                    continue;
                }

                pthread_t thread;
                int rc = pthread_create(&thread, NULL, thread_conn_worker, slot);

                if (rc != 0) {
                    /*
                     * No worker was created, so release the socket and slot.
                     * pthread_create returns an error number directly.
                     */
                    close(conn_fd);

                    pthread_mutex_lock(&conn_mutex);
                    slot->fd = -1;
                    slot->adapter = NULL;
                    slot->rx_len = 0;
                    pthread_mutex_unlock(&conn_mutex);

                    fprintf(stderr, "pthread_create: %s\n", strerror(rc));
                    continue;
                }

                /* The worker releases its slot when the connection ends. */
                rc = pthread_detach(thread);
                if (rc != 0) {
                    fprintf(stderr, "pthread_detach: %s\n", strerror(rc));
                }
            }

        }
    }
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
    reset_connection();

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
    conn_accept_and_dispatch();
}
