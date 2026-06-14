/*
 * ThreadSanitizer harness for the daemon's serial event queue.
 *
 * The push/pop below are kept identical to event_queue_push/event_queue_pop in
 * millennium_sdk.c. In the daemon the queue is produced by both the main loop
 * (serial events) and PJSUA worker threads (SIP call-state events) and consumed
 * by the main loop; with no lock those concurrent linked-list/malloc operations
 * corrupt the heap (observed as a SIGABRT under load). This reproduces that and
 * verifies the fix.
 *
 *   make tsan-queue        # builds + runs both variants under ThreadSanitizer
 *
 * Locked build  -> clean (no races).  -DNO_LOCK build -> ThreadSanitizer reports
 * data races on event_queue_head/tail. (Keep in sync with millennium_sdk.c.)
 */
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

struct event_queue_node { void *event; struct event_queue_node *next; };
struct millennium_client {
    struct event_queue_node *event_queue_head, *event_queue_tail;
};

#ifdef NO_LOCK
#define QLOCK()   ((void)0)
#define QUNLOCK() ((void)0)
#else
static pthread_mutex_t g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
#define QLOCK()   pthread_mutex_lock(&g_queue_mutex)
#define QUNLOCK() pthread_mutex_unlock(&g_queue_mutex)
#endif

static void event_queue_push(struct millennium_client *client, void *event) {
    struct event_queue_node *node = malloc(sizeof(struct event_queue_node));
    if (!node) return;
    node->event = event;
    node->next = NULL;
    QLOCK();
    if (client->event_queue_tail) client->event_queue_tail->next = node;
    else client->event_queue_head = node;
    client->event_queue_tail = node;
    QUNLOCK();
}

static void *event_queue_pop(struct millennium_client *client) {
    struct event_queue_node *node;
    void *event;
    QLOCK();
    if (!client->event_queue_head) { QUNLOCK(); return NULL; }
    node = client->event_queue_head;
    event = node->event;
    client->event_queue_head = node->next;
    if (!client->event_queue_head) client->event_queue_tail = NULL;
    QUNLOCK();
    free(node);
    return event;
}

static struct millennium_client cl;
#define N 50000

static void *producer(void *arg) {
    long id = (long)arg, i;
    for (i = 0; i < N; i++) event_queue_push(&cl, (void *)(id * N + i + 1));
    return NULL;
}

/* Bounded pop attempts so the (corrupting) no-lock run still terminates after
 * ThreadSanitizer has flagged the race, instead of spinning on a lost item. */
static void *consumer(void *arg) {
    long tries = 0;
    (void)arg;
    while (tries < 4 * N) { event_queue_pop(&cl); tries++; }
    return NULL;
}

int main(void) {
    pthread_t p1, p2, c1;
    pthread_create(&p1, NULL, producer, (void *)1);  /* main-loop serial pushes */
    pthread_create(&p2, NULL, producer, (void *)2);  /* PJSUA call-state pushes */
    pthread_create(&c1, NULL, consumer, NULL);        /* main-loop consumer      */
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);
    pthread_join(c1, NULL);
    printf("done\n");
    return 0;
}
