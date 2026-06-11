#include "conn_queue.h"

#include <stdlib.h>

int conn_queue_init(struct conn_queue* q, int capacity) {
    if (!q || capacity <= 0) return -1;

    q->fds = (int*)malloc((size_t)capacity * sizeof(int));
    if (!q->fds) return -1;

    q->capacity = capacity;
    q->head = 0;
    q->count = 0;
    q->closed = 0;

    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        free(q->fds);
        q->fds = NULL;
        return -1;
    }
    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&q->mutex);
        free(q->fds);
        q->fds = NULL;
        return -1;
    }
    return 0;
}

void conn_queue_destroy(struct conn_queue* q) {
    if (!q) return;
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->mutex);
    free(q->fds);
    q->fds = NULL;
    q->capacity = 0;
    q->head = 0;
    q->count = 0;
}

int conn_queue_try_push(struct conn_queue* q, int fd) {
    int tail;
    if (!q) return -1;

    pthread_mutex_lock(&q->mutex);
    if (q->closed || q->count >= q->capacity) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    tail = (q->head + q->count) % q->capacity;
    q->fds[tail] = fd;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int conn_queue_pop(struct conn_queue* q) {
    int fd;
    if (!q) return -1;

    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    if (q->count == 0) {
        /* Closed and drained. */
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    fd = q->fds[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
    return fd;
}

void conn_queue_close(struct conn_queue* q) {
    if (!q) return;
    pthread_mutex_lock(&q->mutex);
    q->closed = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

int conn_queue_count(struct conn_queue* q) {
    int c;
    if (!q) return 0;
    pthread_mutex_lock(&q->mutex);
    c = q->count;
    pthread_mutex_unlock(&q->mutex);
    return c;
}
