#include "share_buffer.h"
#include <stdlib.h>
#include "logger.h"

static const char* TAG="SBUF";

static void sbuffer_cleanup(sbuffer_t *buffer)
{
    while (buffer->head &&
           buffer->head->read_by_data &&
           buffer->head->read_by_storage)
    {
        sbuffer_node_t *tmp = buffer->head;
        buffer->head = tmp->next;

        if (buffer->head == NULL)
            buffer->tail = NULL;

        free(tmp);
        ldebug("%s: cleanup, size:%d", TAG, buffer->size);
        buffer->size--;
    }
}

int sbuffer_init(sbuffer_t *buffer)
{
    buffer->head = NULL;
    buffer->tail = NULL;
    buffer->size = 0;
    buffer->shutdown = false;

    pthread_mutex_init(&buffer->lock, NULL);
    pthread_cond_init(&buffer->not_empty, NULL);

    return 0;
}

int sbuffer_insert(sbuffer_t *buffer,
                   const sensor_data_t *data)
{
    sbuffer_node_t *node = malloc(sizeof(*node));
    ldebug("%s: insert data", TAG);

    if (node == NULL)
        return -1;

    node->data = *data;
    node->read_by_data = false;
    node->read_by_storage = false;
    node->next = NULL;

    ldebug("%s: insert mutex lock", TAG);
    pthread_mutex_lock(&buffer->lock);

    if (buffer->tail == NULL) {
        buffer->head = node;
        buffer->tail = node;
    } else {
        buffer->tail->next = node;
        buffer->tail = node;
    }

    buffer->size++;

    ldebug("%s: insert cond_broadcast", TAG);
    pthread_cond_broadcast(&buffer->not_empty);

    ldebug("%s: insert mutex unlock", TAG);
    pthread_mutex_unlock(&buffer->lock);

    return 0;
}

int sbuffer_read(sbuffer_t *buffer,
                 sensor_data_t *data,
                 reader_type_t reader)
{
    pthread_mutex_lock(&buffer->lock);
    ldebug("%s: read lock mutex", TAG);

    while (1)
    {
        sbuffer_node_t *node = buffer->head;

        while (node)
        {
            bool unread =
                (reader == READER_DATA)
                    ? !node->read_by_data
                    : !node->read_by_storage;

            if (unread)
            {
                *data = node->data;

                if (reader == READER_DATA)
                    node->read_by_data = true;
                else
                    node->read_by_storage = true;
                ldebug("%s: call sbuffer_cleanup", TAG);
                sbuffer_cleanup(buffer);
                pthread_mutex_unlock(&buffer->lock);
                return 0;
            }

            node = node->next;
        }

        /*
         * No unread node found
         */
        if (buffer->shutdown)
        {
            ldebug("%s: read shutdown mutex unlock", TAG);
            pthread_mutex_unlock(&buffer->lock);
            return -1;
        }
        ldebug("%s: read cond_wait", TAG);
        pthread_cond_wait(&buffer->not_empty,
                          &buffer->lock);
    }
}

void sbuffer_shutdown(sbuffer_t *buffer)
{
    pthread_mutex_lock(&buffer->lock);

    ldebug("%s: Shutdown", TAG);
    buffer->shutdown = true;
    ldebug("%s: cond broadcast not_empty", TAG);

    pthread_cond_broadcast(&buffer->not_empty);

    pthread_mutex_unlock(&buffer->lock);
}

void sbuffer_destroy(sbuffer_t *buffer)
{
    pthread_mutex_lock(&buffer->lock);

    ldebug("%s: destroy", TAG);
    sbuffer_node_t *curr = buffer->head;

    while (curr)
    {
        sbuffer_node_t *next = curr->next;
        ldebug("%s: free", TAG);
        free(curr);
        curr = next;
    }

    buffer->head = NULL;
    buffer->tail = NULL;
    buffer->size = 0;

    pthread_mutex_unlock(&buffer->lock);

    pthread_mutex_destroy(&buffer->lock);
    pthread_cond_destroy(&buffer->not_empty);
}
