#ifndef CONN_QUEUE_H
#define CONN_QUEUE_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * conn_queue: a small bounded, thread-safe FIFO of file descriptors.
 *
 * The web server's accept loop pushes accepted client fds onto the queue; a
 * pool of worker threads pops and services them. This decouples accepting a
 * connection from handling it, so one slow request no longer stalls every
 * other client (see issue #125).
 *
 * Push is non-blocking and reports back-pressure (queue full) to the caller so
 * the accept loop can shed load rather than grow unboundedly. Pop blocks until
 * an fd is available or the queue is closed.
 */
struct conn_queue {
    int* fds;
    int capacity;
    int head;   /* index of the next fd to pop */
    int count;  /* number of fds currently queued */
    int closed; /* set by conn_queue_close(); pop drains then returns -1 */
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
};

/* Initialize a queue with room for `capacity` fds. Returns 0 on success,
 * -1 on bad argument or allocation failure. */
int conn_queue_init(struct conn_queue* q, int capacity);

/* Free resources. The queue must not be in use by other threads. */
void conn_queue_destroy(struct conn_queue* q);

/* Enqueue an fd without blocking. Returns 0 on success, -1 if the queue is
 * full or closed (the caller still owns `fd` in that case). */
int conn_queue_try_push(struct conn_queue* q, int fd);

/* Dequeue an fd, blocking until one is available. Returns the fd (>= 0), or
 * -1 once the queue is both closed and empty. */
int conn_queue_pop(struct conn_queue* q);

/* Mark the queue closed and wake every blocked popper. After this, pop drains
 * any remaining fds and then returns -1; try_push returns -1. */
void conn_queue_close(struct conn_queue* q);

/* Current number of queued fds (snapshot). */
int conn_queue_count(struct conn_queue* q);

#ifdef __cplusplus
}
#endif

#endif /* CONN_QUEUE_H */
