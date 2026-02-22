#define _POSIX_C_SOURCE 200112L
#include "metrics_server.h"
#include "metrics.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

/* C89-compatible strdup implementation */
static char *my_strdup(const char *s) {
    size_t len;
    char *copy;
    
    if (!s) return NULL;
    
    len = strlen(s) + 1;
    copy = malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

metrics_server_t *metrics_server_create(int port) {
    metrics_server_t *server;
    
    server = malloc(sizeof(metrics_server_t));
    if (!server) return NULL;
    
    memset(server, 0, sizeof(metrics_server_t));
    server->port = port;
    server->running = 0;
    server->should_stop = 0;
    
    if (pthread_mutex_init(&server->state_mutex, NULL) != 0) {
        free(server);
        return NULL;
    }
    
    return server;
}

void metrics_server_destroy(metrics_server_t *server) {
    if (!server) return;
    
    metrics_server_stop(server);
    pthread_mutex_destroy(&server->state_mutex);
    free(server);
}

int metrics_server_start(metrics_server_t *server) {
    if (!server) return -1;
    
    pthread_mutex_lock(&server->state_mutex);
    if (server->running) {
        pthread_mutex_unlock(&server->state_mutex);
        return 0; /* Already running */
    }
    
    server->should_stop = 0;
    server->running = 1;
    
    if (pthread_create(&server->server_thread, NULL, metrics_server_loop, server) != 0) {
        server->running = 0;
        pthread_mutex_unlock(&server->state_mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&server->state_mutex);
    
    logger_infof_with_category("MetricsServer", 
        "Metrics server started on port %d", server->port);
    
    return 0;
}

int metrics_server_stop(metrics_server_t *server) {
    if (!server) return -1;
    
    pthread_mutex_lock(&server->state_mutex);
    if (!server->running) {
        pthread_mutex_unlock(&server->state_mutex);
        return 0; /* Already stopped */
    }
    
    server->should_stop = 1;
    server->running = 0;
    pthread_mutex_unlock(&server->state_mutex);
    
    pthread_join(server->server_thread, NULL);
    
    logger_info_with_category("MetricsServer", "Metrics server stopped");
    
    return 0;
}

int metrics_server_is_running(const metrics_server_t *server) {
    int running;
    
    if (!server) return 0;
    
    pthread_mutex_lock((pthread_mutex_t*)&server->state_mutex);
    running = server->running;
    pthread_mutex_unlock((pthread_mutex_t*)&server->state_mutex);
    
    return running;
}

int metrics_server_set_port(metrics_server_t *server, int port) {
    if (!server) return -1;
    
    pthread_mutex_lock(&server->state_mutex);
    if (server->running) {
        pthread_mutex_unlock(&server->state_mutex);
        logger_warn_with_category("MetricsServer", 
            "Cannot change port while server is running");
        return -1;
    }
    server->port = port;
    pthread_mutex_unlock(&server->state_mutex);
    
    return 0;
}

int metrics_server_get_port(const metrics_server_t *server) {
    int port;
    
    if (!server) return -1;
    
    pthread_mutex_lock((pthread_mutex_t*)&server->state_mutex);
    port = server->port;
    pthread_mutex_unlock((pthread_mutex_t*)&server->state_mutex);
    
    return port;
}

void *metrics_server_loop(void *arg) {
    metrics_server_t *server = (metrics_server_t*)arg;
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int flags;
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    char buffer[1024];
    ssize_t bytes_read;
    char *request;
    char *response;
    
    if (!server) return NULL;
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        logger_error_with_category("MetricsServer", 
            "Failed to create socket");
        return NULL;
    }
    
    /* Set socket options for reuse */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger_error_with_category("MetricsServer", 
            "Failed to set socket options");
        close(server_fd);
        return NULL;
    }
    
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server->port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        logger_errorf_with_category("MetricsServer", 
            "Failed to bind to port %d", server->port);
        close(server_fd);
        return NULL;
    }
    
    if (listen(server_fd, 3) < 0) {
        logger_error_with_category("MetricsServer", 
            "Failed to listen on socket");
        close(server_fd);
        return NULL;
    }
    
    /* Set socket to non-blocking */
    flags = fcntl(server_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    while (1) {
        pthread_mutex_lock(&server->state_mutex);
        if (server->should_stop) {
            pthread_mutex_unlock(&server->state_mutex);
            break;
        }
        pthread_mutex_unlock(&server->state_mutex);
        
        memset(&client_addr, 0, sizeof(client_addr));
        client_len = sizeof(client_addr);
        
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd >= 0) {
            /* Handle request in a simple way (no threading to save resources) */
            memset(buffer, 0, sizeof(buffer));
            bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                request = malloc(bytes_read + 1);
                if (request) {
                    memcpy(request, buffer, bytes_read);
                    request[bytes_read] = '\0';
                    
                    response = metrics_server_handle_request(server, request);
                    if (response) {
                        metrics_server_send_response(client_fd, response);
                        free(response);
                    }
                    free(request);
                }
            }
            
            close(client_fd);
        }
        
        /* Small delay to prevent busy waiting */
        /* Simple delay loop - not precise but works in C89 */
        {
            volatile int i;
            for (i = 0; i < 10000; i++) {
                /* Empty loop for delay */
            }
        }
    }
    
    close(server_fd);
    return NULL;
}

char *metrics_server_handle_request(metrics_server_t *server, const char *request) {
    if (!server || !request) return NULL;
    
    /* Simple request parsing */
    if (strstr(request, "GET /metrics") != NULL) {
        return metrics_server_generate_metrics_response(server);
    } else if (strstr(request, "GET /health") != NULL) {
        return metrics_server_generate_health_response(server);
    } else if (strstr(request, "GET /") != NULL) {
        /* Default to metrics for any GET request */
        return metrics_server_generate_metrics_response(server);
    }
    
    /* Default response */
    return my_strdup("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
}

char *metrics_server_generate_metrics_response(metrics_server_t *server) {
    char *prometheus_data;
    char *response;
    size_t response_len;
    
    if (!server) return NULL;
    
    prometheus_data = metrics_export_prometheus();
    if (!prometheus_data) return NULL;
    
    response_len = strlen(prometheus_data) + 200; /* Extra space for headers */
    response = malloc(response_len);
    if (!response) {
        free(prometheus_data);
        return NULL;
    }
    
    snprintf(response, response_len,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        strlen(prometheus_data), prometheus_data);
    
    free(prometheus_data);
    return response;
}

char *metrics_server_generate_health_response(metrics_server_t *server) {
    char *response;
    
    if (!server) return NULL;
    
    response = malloc(200);
    if (!response) return NULL;
    
    snprintf(response, 200,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 15\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"status\":\"ok\"}");
    
    return response;
}

int metrics_server_send_response(int client_fd, const char *response) {
    ssize_t bytes_sent;
    size_t response_len;
    
    if (client_fd < 0 || !response) return -1;
    
    response_len = strlen(response);
    bytes_sent = send(client_fd, response, response_len, 0);
    
    return (bytes_sent == (ssize_t)response_len) ? 0 : -1;
}
