#ifndef METRICS_SERVER_H
#define METRICS_SERVER_H

#include <stdint.h>
#include <pthread.h>

/* Forward declarations */
typedef struct metrics_server metrics_server_t;

/* Metrics server structure */
struct metrics_server {
    int port;
    int running;
    int should_stop;
    pthread_t server_thread;
    pthread_mutex_t state_mutex;
};

/* Constructor and destructor */
metrics_server_t *metrics_server_create(int port);
void metrics_server_destroy(metrics_server_t *server);

/* Server control */
int metrics_server_start(metrics_server_t *server);
int metrics_server_stop(metrics_server_t *server);
int metrics_server_is_running(const metrics_server_t *server);

/* Configuration */
int metrics_server_set_port(metrics_server_t *server, int port);
int metrics_server_get_port(const metrics_server_t *server);

/* Internal methods (used by server thread) */
void *metrics_server_loop(void *arg);
char *metrics_server_handle_request(metrics_server_t *server, const char *request);
char *metrics_server_generate_metrics_response(metrics_server_t *server);
char *metrics_server_generate_health_response(metrics_server_t *server);
int metrics_server_send_response(int client_fd, const char *response);

#endif /* METRICS_SERVER_H */