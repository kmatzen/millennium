#define _POSIX_C_SOURCE 200112L
#include "web_server.h"
#include "config.h"
#include "logger.h"
#include "metrics.h"
#include "health_monitor.h"
#include "plugins.h"
#include "display_manager.h"
#include "websocket.h"
#include "version.h"
#include "updater.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>

/* External references to daemon state (will be set by daemon) */
extern void* daemon_state;
extern void* client;

/* Daemon state structure is now defined in web_server.h */

/* Function declarations for daemon integration */
struct daemon_state_info get_daemon_state_info(void);
time_t get_daemon_start_time(void);
int send_control_command(const char* action);

/* Thread function for web server */
void* web_server_thread_func(void* arg);
static void* web_server_worker_func(void* arg);

/* String utility functions */
void web_server_strcpy_safe(char* dest, const char* src, size_t dest_size) {
    size_t i;
    if (!dest || !src || dest_size == 0) return;

    for (i = 0; i < dest_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

void web_server_strcat_safe(char* dest, const char* src, size_t dest_size) {
    size_t dest_len;
    size_t i;
    if (!dest || !src || dest_size == 0) return;

    dest_len = web_server_strlen_safe(dest);
    if (dest_len >= dest_size - 1) return;

    for (i = 0; i < dest_size - dest_len - 1 && src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';
}

int web_server_strcmp_safe(const char* str1, const char* str2) {
    if (!str1 && !str2) return 0;
    if (!str1) return -1;
    if (!str2) return 1;
    return strcmp(str1, str2);
}

char* web_server_strstr_safe(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    return strstr(haystack, needle);
}

size_t web_server_strlen_safe(const char* str) {
    if (!str) return 0;
    return strlen(str);
}

/* JSON escape: escape " and \ for safe embedding in JSON string values */
static void web_server_json_escape(const char* src, char* dest, size_t dest_size) {
    const char* s;
    char* d;
    if (!src || !dest || dest_size == 0) return;
    for (s = src, d = dest; *s && (size_t)(d - dest) < dest_size - 2; s++) {
        if (*s == '"' || *s == '\\') *d++ = '\\';
        *d++ = *s;
    }
    *d = '\0';
}

/* Memory management */
void* web_server_malloc(size_t size) {
    return malloc(size);
}

void web_server_free(void* ptr) {
    if (ptr) free(ptr);
}

/* Case-insensitive string comparison for C89 compatibility */
int web_server_strcasecmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    
    while (*s1 && *s2) {
        char c1 = tolower(*s1);
        char c2 = tolower(*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower(*s1) - tolower(*s2);
}

/* WebServer creation and destruction */
struct web_server* web_server_create(int port) {
    int i;
    struct web_server* server = (struct web_server*)web_server_malloc(sizeof(struct web_server));
    if (!server) return NULL;

    memset(server, 0, sizeof(struct web_server));
    server->port = port;
    server->running = 0;
    server->should_stop = 0;
    server->paused = 0;
    server->server_fd = -1;
    server->route_count = 0;
    server->static_count = 0;
    server->websocket_count = 0;
    server->rate_limit_count = 0;
    server->worker_count = 0;

    if (conn_queue_init(&server->conn_queue, WEB_SERVER_QUEUE_DEPTH) != 0) {
        logger_error_with_category("WebServer", "Failed to init connection queue");
        web_server_free(server);
        return NULL;
    }
    if (pthread_mutex_init(&server->state_mutex, NULL) != 0) {
        logger_error_with_category("WebServer", "Failed to init state mutex");
        conn_queue_destroy(&server->conn_queue);
        web_server_free(server);
        return NULL;
    }

    /* Initialize static route flags */
    for (i = 0; i < 16; i++) {
        server->static_is_file[i] = 0;
    }

    return server;
}

void web_server_destroy(struct web_server* server) {
    if (!server) return;
    
    web_server_stop(server);
    conn_queue_destroy(&server->conn_queue);
    pthread_mutex_destroy(&server->state_mutex);
    web_server_free(server);
}

/* WebServer control functions */
void web_server_start(struct web_server* server) {
    if (!server || server->running) return;
    
    server->should_stop = 0;
    server->running = 1;
    
    /* Setup API routes */
    web_server_setup_api_routes(server);
    
    /* Initialize server socket */
    web_server_init_socket(server);
    
    /* Start worker pool before the accept thread so a connection is never
     * enqueued with no one to service it. */
    server->worker_count = 0;
    {
        int w;
        for (w = 0; w < WEB_SERVER_WORKER_COUNT; w++) {
            if (pthread_create(&server->worker_threads[w], NULL,
                               web_server_worker_func, server) != 0) {
                logger_error_with_category("WebServer", "Failed to create worker thread");
                break;
            }
            server->worker_count++;
        }
    }
    if (server->worker_count == 0) {
        logger_error_with_category("WebServer", "No worker threads started; aborting web server");
        server->running = 0;
        if (server->server_fd >= 0) {
            close(server->server_fd);
            server->server_fd = -1;
        }
        return;
    }

    /* Start the accept thread */
    if (pthread_create(&server->server_thread, NULL, web_server_thread_func, server) != 0) {
        logger_error_with_category("WebServer", "Failed to create server thread");
        /* Unwind the workers we just started. */
        conn_queue_close(&server->conn_queue);
        {
            int w;
            for (w = 0; w < server->worker_count; w++) {
                pthread_join(server->worker_threads[w], NULL);
            }
        }
        server->worker_count = 0;
        server->running = 0;
        if (server->server_fd >= 0) {
            close(server->server_fd);
            server->server_fd = -1;
        }
    }
}

void web_server_stop(struct web_server* server) {
    if (!server || !server->running) return;
    
    server->should_stop = 1;

    /* Stop accepting first: the accept thread sees should_stop and returns. */
    pthread_join(server->server_thread, NULL);

    /* Then drain the worker pool: close the queue so blocked workers wake and
     * exit once any remaining queued connections are serviced. */
    conn_queue_close(&server->conn_queue);
    {
        int w;
        for (w = 0; w < server->worker_count; w++) {
            pthread_join(server->worker_threads[w], NULL);
        }
    }
    server->worker_count = 0;

    server->running = 0;

    if (server->server_fd >= 0) {
        close(server->server_fd);
        server->server_fd = -1;
    }

    logger_info_with_category("WebServer", "Web server stopped");
}

void web_server_pause(struct web_server* server) {
    if (!server) return;
    server->paused = 1;
    logger_info_with_category("WebServer", "Web server paused for audio protection");
}

void web_server_resume(struct web_server* server) {
    if (!server) return;
    server->paused = 0;
    logger_info_with_category("WebServer", "Web server resumed");
}

int web_server_is_running(const struct web_server* server) {
    return server ? server->running : 0;
}

int web_server_is_paused(const struct web_server* server) {
    return server ? server->paused : 0;
}

void web_server_get_conn_stats(struct web_server* server, struct conn_queue_stats* out) {
    if (!out) return;
    /* conn_queue_get_stats zeroes the output when handed a NULL queue. */
    conn_queue_get_stats(server ? &server->conn_queue : NULL, out);
}

void web_server_set_port(struct web_server* server, int port) {
    if (!server) return;
    
    if (server->running) {
        logger_warn_with_category("WebServer", "Cannot change port while server is running");
        return;
    }
    server->port = port;
}

int web_server_get_port(const struct web_server* server) {
    return server ? server->port : 0;
}

/* Route management */
void web_server_add_route(struct web_server* server, const char* method, const char* path, route_handler_t handler) {
    int idx;
    if (!server || !method || !path || !handler) return;
    if (server->route_count >= 32) return; /* Max routes reached */

    idx = server->route_count;
    web_server_strcpy_safe(server->route_methods[idx], method, sizeof(server->route_methods[idx]));
    web_server_strcpy_safe(server->route_paths[idx], path, sizeof(server->route_paths[idx]));
    server->route_handlers[idx] = handler;
    server->route_count++;
}

void web_server_add_static_route(struct web_server* server, const char* path, const char* content, const char* content_type) {
    int idx;
    if (!server || !path || !content || !content_type) return;
    if (server->static_count >= 16) return; /* Max static routes reached */

    idx = server->static_count;
    web_server_strcpy_safe(server->static_paths[idx], path, sizeof(server->static_paths[idx]));
    web_server_strcpy_safe(server->static_contents[idx], content, sizeof(server->static_contents[idx]));
    web_server_strcpy_safe(server->static_content_types[idx], content_type, sizeof(server->static_content_types[idx]));
    server->static_is_file[idx] = 0;  /* Using content, not file */
    server->static_count++;
}

void web_server_add_file_route(struct web_server* server, const char* path, const char* file_path, const char* content_type) {
    int idx;
    if (!server || !path || !file_path || !content_type) return;
    if (server->static_count >= 16) return; /* Max static routes reached */

    idx = server->static_count;
    web_server_strcpy_safe(server->static_paths[idx], path, sizeof(server->static_paths[idx]));
    web_server_strcpy_safe(server->static_file_paths[idx], file_path, sizeof(server->static_file_paths[idx]));
    web_server_strcpy_safe(server->static_content_types[idx], content_type, sizeof(server->static_content_types[idx]));
    server->static_is_file[idx] = 1;  /* Using file path */
    server->static_count++;
}

/* HTTP parsing functions */
struct http_request web_server_parse_request(const char* raw_request) {
    struct http_request request;
    const char* line_start;
    const char* line_end;
    char request_line[512];
    size_t line_len;
    char* cr_pos;
    char* query_pos;
    const char* header_start;
    memset(&request, 0, sizeof(request));

    if (!raw_request) return request;

    /* Parse request line */
    line_start = raw_request;
    line_end = strchr(line_start, '\n');
    if (!line_end) return request;

    /* Extract method and path */
    line_len = line_end - line_start;
    if (line_len >= sizeof(request_line)) line_len = sizeof(request_line) - 1;

    memcpy(request_line, line_start, line_len);
    request_line[line_len] = '\0';

    /* Remove \r if present */
    cr_pos = strchr(request_line, '\r');
    if (cr_pos) *cr_pos = '\0';

    /* Parse method and path */
    if (sscanf(request_line, "%15s %255s", request.method, request.path) != 2) {
        return request;
    }

    /* Remove query parameters from path */
    query_pos = strchr(request.path, '?');
    if (query_pos) {
        *query_pos = '\0';
        web_server_parse_query_string(query_pos + 1, &request);
    }

    /* Parse headers */
    header_start = line_end + 1;
    while (header_start && *header_start != '\0') {
        const char* header_end;
        char header_line[512];
        size_t header_len;
        char* cr_pos;
        char* colon_pos;
        header_end = strchr(header_start, '\n');
        if (!header_end) break;

        /* Check for end of headers (empty line) */
        if (header_end == header_start + 1) {
            break;
        }

        header_len = header_end - header_start;
        if (header_len >= sizeof(header_line)) header_len = sizeof(header_line) - 1;

        memcpy(header_line, header_start, header_len);
        header_line[header_len] = '\0';

        /* Remove \r if present */
        cr_pos = strchr(header_line, '\r');
        if (cr_pos) *cr_pos = '\0';

        /* Parse header key:value */
        colon_pos = strchr(header_line, ':');
        if (colon_pos && request.header_count < 32) {
            char* key;
            char* value;
            char* key_end;
            char* value_end;
            *colon_pos = '\0';
            key = header_line;
            value = colon_pos + 1;

            /* Trim whitespace */
            while (*key == ' ' || *key == '\t') key++;
            while (*value == ' ' || *value == '\t') value++;

            key_end = key + strlen(key) - 1;
            while (key_end > key && (*key_end == ' ' || *key_end == '\t' || *key_end == '\r')) {
                *key_end = '\0';
                key_end--;
            }

            value_end = value + strlen(value) - 1;
            while (value_end > value && (*value_end == ' ' || *value_end == '\t' || *value_end == '\r')) {
                *value_end = '\0';
                value_end--;
            }
            
            web_server_strcpy_safe(request.header_keys[request.header_count], key, sizeof(request.header_keys[request.header_count]));
            web_server_strcpy_safe(request.header_values[request.header_count], value, sizeof(request.header_values[request.header_count]));
            request.header_count++;
        }
        
        header_start = header_end + 1;
    }
    
    /* Parse body */
    if (header_start && *header_start != '\0') {
        size_t body_len = strlen(header_start);
        if (body_len >= sizeof(request.body)) body_len = sizeof(request.body) - 1;
        memcpy(request.body, header_start, body_len);
        request.body[body_len] = '\0';
    }
    
    return request;
}

void web_server_parse_query_string(const char* query, struct http_request* request) {
    const char* start;
    if (!query || !request) return;

    start = query;
    while (*start && request->query_count < 16) {
        const char* end;
        char pair[256];
        size_t pair_len;
        char* eq_pos;
        end = strchr(start, '&');
        if (!end) end = start + strlen(start);

        pair_len = end - start;
        if (pair_len >= sizeof(pair)) pair_len = sizeof(pair) - 1;

        memcpy(pair, start, pair_len);
        pair[pair_len] = '\0';

        eq_pos = strchr(pair, '=');
        if (eq_pos) {
            char* key;
            char* value;
            char decoded_key[64];
            char decoded_value[256];
            *eq_pos = '\0';
            key = pair;
            value = eq_pos + 1;

            /* URL decode */
            web_server_url_decode(key, decoded_key, sizeof(decoded_key));
            web_server_url_decode(value, decoded_value, sizeof(decoded_value));
            
            web_server_strcpy_safe(request->query_keys[request->query_count], decoded_key, sizeof(request->query_keys[request->query_count]));
            web_server_strcpy_safe(request->query_values[request->query_count], decoded_value, sizeof(request->query_values[request->query_count]));
            request->query_count++;
        }
        
        if (*end == '\0') break;
        start = end + 1;
    }
}

char* web_server_url_decode(const char* str, char* result, size_t result_size) {
    size_t i, j;
    if (!str || !result || result_size == 0) return NULL;

    j = 0;
    for (i = 0; str[i] != '\0' && j < result_size - 1; i++) {
        if (str[i] == '%' && i + 2 < strlen(str)) {
            char hex[3] = {str[i+1], str[i+2], '\0'};
            int value = strtol(hex, NULL, 16);
            if (value > 0) {
                result[j++] = (char)value;
                i += 2;
            } else {
                result[j++] = str[i];
            }
        } else if (str[i] == '+') {
            result[j++] = ' ';
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

/* Socket initialization */
void web_server_init_socket(struct web_server* server) {
    int opt;
    struct sockaddr_in address;
    int flags;
    char start_msg[256];
    if (!server) return;

    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        logger_error_with_category("WebServer", "Failed to create socket");
        return;
    }

    /* Set socket options for reuse */
    opt = 1;
    setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server->port);
    
    if (bind(server->server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to bind to port %d", server->port);
        logger_error_with_category("WebServer", error_msg);
        close(server->server_fd);
        server->server_fd = -1;
        return;
    }
    
    if (listen(server->server_fd, 10) < 0) {
        logger_error_with_category("WebServer", "Failed to listen on socket");
        close(server->server_fd);
        server->server_fd = -1;
        return;
    }
    
    /* Set socket to non-blocking */
    flags = fcntl(server->server_fd, F_GETFL, 0);
    fcntl(server->server_fd, F_SETFL, flags | O_NONBLOCK);

    snprintf(start_msg, sizeof(start_msg), "Web server started on port %d", server->port);
    logger_info_with_category("WebServer", start_msg);
}

/* Short reply sent when the worker queue is saturated, so a flooded server
 * sheds load with a proper 503 instead of silently dropping the connection. */
static void web_server_send_busy(int client_fd) {
    static const char* busy =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: application/json\r\n"
        "Retry-After: 1\r\n"
        "Connection: close\r\n"
        "Content-Length: 23\r\n"
        "\r\n"
        "{\"error\":\"Server busy\"}";
    send(client_fd, busy, strlen(busy), 0);
}

/* Accept thread: accepts connections and hands each off to a worker via the
 * connection queue. Accepting stays cheap so the listen backlog drains quickly
 * even while workers are busy with slow requests (#125). */
void* web_server_thread_func(void* arg) {
    struct web_server* server = (struct web_server*)arg;
    if (!server) return NULL;

    while (!server->should_stop) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        struct timeval tv;
        int client_fd = accept(server->server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd >= 0) {
            if (conn_queue_try_push(&server->conn_queue, client_fd) != 0) {
                /* Backlog full: shed load rather than grow unboundedly. */
                logger_warn_with_category("WebServer",
                    "Connection queue full; rejecting client with 503");
                web_server_send_busy(client_fd);
                close(client_fd);
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            /* Log error if it's not just a non-blocking accept */
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Accept failed with error: %d", errno);
            logger_error_with_category("WebServer", error_msg);
        }

        /* Small sleep to prevent busy waiting */
        /* Use select() for C89-compatible microsecond sleep */
        tv.tv_sec = 0;
        tv.tv_usec = 1000; /* 1ms */
        select(0, NULL, NULL, NULL, &tv);
    }

    return NULL;
}

/* Worker thread: services one accepted connection at a time, pulled from the
 * shared queue. Multiple workers run concurrently, so a slow or expensive
 * request only occupies its own worker instead of blocking every client. */
static void* web_server_worker_func(void* arg) {
    struct web_server* server = (struct web_server*)arg;
    if (!server) return NULL;

    for (;;) {
        int client_fd = conn_queue_pop(&server->conn_queue);
        if (client_fd < 0) {
            break; /* Queue closed and drained: shut the worker down. */
        }
        web_server_handle_client(server, client_fd);
    }

    return NULL;
}

/* Case-insensitively scan request text line-by-line for the Content-Length
 * header and return its value, or -1 if absent/malformed. Scanning per-line
 * (not a raw substring search) avoids matching the token inside a body. */
static long web_server_parse_content_length(const char* buf) {
    const char* p = buf;
    while (p && *p) {
        const char* h = p;
        const char* n = "content-length:";
        while (*n && *h && (char)tolower((unsigned char)*h) == *n) {
            h++;
            n++;
        }
        if (*n == '\0') {
            while (*h == ' ' || *h == '\t') h++;
            return strtol(h, NULL, 10);
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return -1;
}

void web_server_handle_client(struct web_server* server, int client_fd) {
    char buffer[4096];
    ssize_t bytes_read;
    size_t total = 0;
    char* header_end = NULL;
    long content_length = -1;
    struct timeval tv;
    if (!server) {
        close(client_fd);
        return;
    }

    /* A request's headers and body can arrive in separate TCP segments, so a
     * single read() often returns only the headers with the body still in
     * flight. That left POST bodies empty — the cause of /api/control
     * intermittently reporting "Unknown action: unknown". Read until the whole
     * body (per Content-Length) has arrived. A receive timeout keeps a slow or
     * stalled client from hanging the single-threaded accept loop. */
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(buffer, 0, sizeof(buffer));
    while (total < sizeof(buffer) - 1) {
        bytes_read = read(client_fd, buffer + total, sizeof(buffer) - 1 - total);
        if (bytes_read <= 0) {
            break;   /* EOF, error, or receive timeout */
        }
        total += (size_t)bytes_read;
        buffer[total] = '\0';

        if (!header_end) {
            header_end = strstr(buffer, "\r\n\r\n");
            if (header_end) {
                header_end += 4;
            } else {
                char* alt = strstr(buffer, "\n\n");
                if (alt) header_end = alt + 2;
            }
            if (header_end) {
                content_length = web_server_parse_content_length(buffer);
            }
        }
        if (header_end) {
            /* No body expected (e.g. GET): headers alone are enough. */
            if (content_length < 0) {
                break;
            }
            /* Stop once the declared body length has been received. */
            if ((size_t)(buffer + total - header_end) >= (size_t)content_length) {
                break;
            }
        }
    }

    if (total > 0) {
        struct http_request parsed_request = web_server_parse_request(buffer);
        struct http_response response;

        /* Get client IP for rate limiting */
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        /* inet_ntop (not inet_ntoa) so concurrent workers don't race on a
         * shared static buffer. */
        if (getpeername(client_fd, (struct sockaddr*)&client_addr, &client_len) == 0 &&
            inet_ntop(AF_INET, &client_addr.sin_addr, parsed_request.client_ip,
                      sizeof(parsed_request.client_ip)) != NULL) {
            /* client_ip populated by inet_ntop */
        } else {
            web_server_strcpy_safe(parsed_request.client_ip, "unknown", sizeof(parsed_request.client_ip));
        }

        response = web_server_process_request(server, &parsed_request);
        
        if (response.status_code == 101) {
            /* WebSocket upgrade — send handshake then keep fd open */
            char* response_str = web_server_serialize_response(&response);
            if (response_str) {
                send(client_fd, response_str, strlen(response_str), 0);
                web_server_free(response_str);
            }
            pthread_mutex_lock(&server->state_mutex);
            if (server->websocket_count < 32) {
                int total;
                server->websocket_connections[server->websocket_count++] = client_fd;
                total = server->websocket_count;
                pthread_mutex_unlock(&server->state_mutex);
                logger_infof_with_category("WebServer",
                    "WebSocket client connected (fd=%d, total=%d)",
                    client_fd, total);
            } else {
                pthread_mutex_unlock(&server->state_mutex);
                logger_warn_with_category("WebServer", "Max WebSocket connections reached");
                close(client_fd);
            }
            return;
        } else if (response.is_streaming) {
            web_server_send_streaming_response(client_fd, &response);
        } else {
            char* response_str = web_server_serialize_response(&response);
            if (response_str) {
                send(client_fd, response_str, strlen(response_str), 0);
                web_server_free(response_str);
            }
        }
    }
    
    close(client_fd);
}

/* Request processing */
struct http_response web_server_process_request(struct web_server* server, const struct http_request* request) {
    struct http_response response;
    int i;
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    
    if (!server || !request) {
        response.status_code = 500;
        return response;
    }
    
    /* When paused (e.g. during audio), reject requests (#122) */
    if (server->paused) {
        response.status_code = 503;
        web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
        web_server_strcpy_safe(response.body, "{\"error\":\"Server paused for audio\"}", sizeof(response.body));
        return response;
    }
    
    /* Apply rate limiting for API endpoints */
    if (strncmp(request->path, "/api/", 5) == 0) {
        if (!web_server_check_rate_limit(server, request->client_ip, request->path)) {
            return web_server_create_rate_limit_response();
        }
    }
    
    /* Check for WebSocket upgrade */
    if (web_server_is_websocket_upgrade(request)) {
        const char *ws_key = NULL;
        char accept_key[64];
        int hi;

        for (hi = 0; hi < request->header_count; hi++) {
            if (web_server_strcasecmp(request->header_keys[hi], "Sec-WebSocket-Key") == 0) {
                ws_key = request->header_values[hi];
                break;
            }
        }

        if (ws_key && ws_compute_accept_key(ws_key, accept_key, sizeof(accept_key)) == 0) {
            response.status_code = 101;
            web_server_strcpy_safe(response.header_keys[response.header_count], "Upgrade", sizeof(response.header_keys[response.header_count]));
            web_server_strcpy_safe(response.header_values[response.header_count], "websocket", sizeof(response.header_values[response.header_count]));
            response.header_count++;
            web_server_strcpy_safe(response.header_keys[response.header_count], "Connection", sizeof(response.header_keys[response.header_count]));
            web_server_strcpy_safe(response.header_values[response.header_count], "Upgrade", sizeof(response.header_values[response.header_count]));
            response.header_count++;
            web_server_strcpy_safe(response.header_keys[response.header_count], "Sec-WebSocket-Accept", sizeof(response.header_keys[response.header_count]));
            web_server_strcpy_safe(response.header_values[response.header_count], accept_key, sizeof(response.header_values[response.header_count]));
            response.header_count++;
        }
        return response;
    }
    
    /* Check static routes first */
    for (i = 0; i < server->static_count; i++) {
        if (strcmp(server->static_paths[i], request->path) == 0) {
            if (server->static_is_file[i]) {
                /* File route - create streaming response */
                FILE* file;
                response.is_streaming = 1;
                web_server_strcpy_safe(response.file_path, server->static_file_paths[i], sizeof(response.file_path));
                web_server_strcpy_safe(response.content_type, server->static_content_types[i], sizeof(response.content_type));

                /* Get file size for Content-Length header */
                file = fopen(response.file_path, "rb");
                if (file) {
                    fseek(file, 0, SEEK_END);
                    response.content_length = ftell(file);
                    fclose(file);
                } else {
                    response.content_length = 0;
                }
            } else {
                /* Content route - use existing behavior */
                response.is_streaming = 0;
                web_server_strcpy_safe(response.body, server->static_contents[i], sizeof(response.body));
                web_server_strcpy_safe(response.content_type, server->static_content_types[i], sizeof(response.content_type));
            }
            return response;
        }
    }
    
    /* Check dynamic routes */
    for (i = 0; i < server->route_count; i++) {
        if (strcmp(server->route_methods[i], request->method) == 0 && 
            strcmp(server->route_paths[i], request->path) == 0) {
            return server->route_handlers[i](request);
        }
    }
    
    /* Default to 404 */
    return web_server_handle_not_found(request);
}

int web_server_is_websocket_upgrade(const struct http_request* request) {
    int i;
    int has_upgrade = 0, has_connection = 0;
    if (!request) return 0;

    for (i = 0; i < request->header_count; i++) {
        if (web_server_strcasecmp(request->header_keys[i], "Upgrade") == 0) {
            if (web_server_strcasecmp(request->header_values[i], "websocket") == 0) {
                has_upgrade = 1;
            }
        } else if (web_server_strcasecmp(request->header_keys[i], "Connection") == 0) {
            if (strstr(request->header_values[i], "Upgrade") != NULL) {
                has_connection = 1;
            }
        }
    }
    
    return has_upgrade && has_connection;
}

/* Response serialization */
char* web_server_serialize_response(const struct http_response* response) {
    size_t total_size;
    char* result;
    char* ptr;
    size_t remaining;
    const char* status_text = "OK";
    int written;
    int i;
    size_t body_len;
    if (!response) return NULL;

    /* Calculate total size needed */
    total_size = 1024; /* Base size for status line and headers */
    total_size += strlen(response->body);
    total_size += response->header_count * 128; /* Space for headers */

    result = (char*)web_server_malloc(total_size);
    if (!result) return NULL;

    ptr = result;
    remaining = total_size;

    /* Status line */
    switch (response->status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 101: status_text = "Switching Protocols"; break;
        case 429: status_text = "Too Many Requests"; break;
        default: status_text = "Unknown"; break;
    }
    
    written = snprintf(ptr, remaining, "HTTP/1.1 %d %s\r\n", response->status_code, status_text);
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }

    /* Headers */
    for (i = 0; i < response->header_count; i++) {
        written = snprintf(ptr, remaining, "%s: %s\r\n", 
                          response->header_keys[i], response->header_values[i]);
        if (written > 0 && (size_t)written < remaining) {
            ptr += written;
            remaining -= written;
        }
    }
    
    /* Content-Type header if not already set */
    if (strlen(response->content_type) > 0) {
        written = snprintf(ptr, remaining, "Content-Type: %s\r\n", response->content_type);
        if (written > 0 && (size_t)written < remaining) {
            ptr += written;
            remaining -= written;
        }
    }
    
    /* Content-Length header */
    written = snprintf(ptr, remaining, "Content-Length: %lu\r\n", (unsigned long)strlen(response->body));
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Connection header */
    written = snprintf(ptr, remaining, "Connection: close\r\n\r\n");
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Body */
    body_len = strlen(response->body);
    if (body_len < remaining) {
        memcpy(ptr, response->body, body_len);
        ptr += body_len;
        *ptr = '\0';
    }
    
    return result;
}

/* API route setup */
void web_server_setup_api_routes(struct web_server* server) {
    if (!server) return;
    
    /* API routes */
    web_server_add_route(server, "GET", "/api/status", web_server_handle_api_status);
    web_server_add_route(server, "GET", "/api/metrics", web_server_handle_api_metrics);
    web_server_add_route(server, "GET", "/api/health", web_server_handle_api_health);
    web_server_add_route(server, "GET", "/api/config", web_server_handle_api_config);
    web_server_add_route(server, "GET", "/api/state", web_server_handle_api_state);
    web_server_add_route(server, "POST", "/api/control", web_server_handle_api_control);
    web_server_add_route(server, "GET", "/api/logs", web_server_handle_api_logs);
    web_server_add_route(server, "GET", "/api/plugins", web_server_handle_api_plugins);
    web_server_add_route(server, "GET", "/api/version", web_server_handle_api_version);
    web_server_add_route(server, "GET", "/api/check-update", web_server_handle_api_check_update);
    web_server_add_route(server, "POST", "/api/update", web_server_handle_api_update);
    web_server_add_route(server, "GET", "/", web_server_handle_dashboard);
}

/* Default handlers */
struct http_response web_server_handle_not_found(const struct http_request* request) {
    struct http_response response;
    const char* html;
    (void)request; /* Suppress unused parameter warning */
    memset(&response, 0, sizeof(response));
    response.status_code = 404;
    web_server_strcpy_safe(response.content_type, "text/html", sizeof(response.content_type));

    html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>404 - Not Found</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }\n"
        "        h1 { color: #e74c3c; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>404 - Page Not Found</h1>\n"
        "    <p>The requested resource was not found.</p>\n"
        "    <a href=\"/\">Return to Dashboard</a>\n"
        "</body>\n"
        "</html>\n";
    
    web_server_strcpy_safe(response.body, html, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_status(const struct http_request* request) {
    struct http_response response;
    time_t now;
    time_t start_time;
    time_t uptime_seconds;
    char json[512];
    (void)request; /* Suppress unused parameter warning */
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    /* Calculate actual uptime */
    now = time(NULL);
    start_time = get_daemon_start_time();
    uptime_seconds = now - start_time;

    snprintf(json, sizeof(json),
        "{"
        "\"status\":\"running\","
        "\"uptime\":%ld,"
        "\"version\":\"1.0.0\","
        "\"timestamp\":%ld"
        "}", uptime_seconds, now);
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_metrics(const struct http_request* request) {
    struct http_response response;
    char *json_data;
    (void)request; /* Suppress unused parameter warning */
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    /* Use C89 metrics API - simplified version */
    json_data = metrics_export_json();
    if (json_data) {
        web_server_strcpy_safe(response.body, json_data, sizeof(response.body));
        free(json_data);
    } else {
        web_server_strcpy_safe(response.body, "{\"error\":\"Failed to export metrics\"}", sizeof(response.body));
    }
    return response;
}

struct http_response web_server_handle_api_health(const struct http_request* request) {
    struct http_response response;
    health_status_t overall_status;
    health_check_t checks[32];
    int checks_count;
    char escaped_overall[32];
    char json[2048];
    char* ptr;
    size_t remaining;
    int written;
    int i;
    int first = 1;
    (void)request; /* Suppress unused parameter warning */
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    overall_status = health_monitor_get_overall_status();
    checks_count = health_monitor_get_all_checks(checks, 32);

    web_server_json_escape(health_monitor_status_to_string(overall_status),
                          escaped_overall, sizeof(escaped_overall));
    ptr = json;
    remaining = sizeof(json);

    written = snprintf(ptr, remaining, "{\"overall_status\":\"%s\",\"checks\":{",
                          escaped_overall);
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }

    for (i = 0; i < checks_count && remaining > 200; i++) {
        char escaped_name[64];
        char escaped_status[32];
        char escaped_message[256];
        web_server_json_escape(checks[i].name, escaped_name, sizeof(escaped_name));
        web_server_json_escape(health_monitor_status_to_string(checks[i].last_status),
                              escaped_status, sizeof(escaped_status));
        web_server_json_escape(checks[i].last_message, escaped_message, sizeof(escaped_message));
        if (!first) {
            written = snprintf(ptr, remaining, ",");
            if (written > 0 && (size_t)written < remaining) {
                ptr += written;
                remaining -= written;
            }
        }
        written = snprintf(ptr, remaining,
            "\"%s\":{\"status\":\"%s\",\"message\":\"%s\",\"last_check\":%ld}",
            escaped_name, escaped_status, escaped_message, checks[i].last_check_time);
        if (written > 0 && (size_t)written < remaining) {
            ptr += written;
            remaining -= written;
        }
        first = 0;
    }
    
    written = snprintf(ptr, remaining, "}}");
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_config(const struct http_request* request) {
    struct http_response response;
    config_data_t* config;
    char escaped_device[128];
    char escaped_level[32];
    char escaped_file[256];
    char json[2048];
    (void)request; /* Suppress unused parameter warning */
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    config = config_get_instance();
    if (!config) {
        web_server_strcpy_safe(response.body, "{\"error\":\"Config not available\"}", sizeof(response.body));
        return response;
    }

    web_server_json_escape(config_get_display_device(config), escaped_device, sizeof(escaped_device));
    web_server_json_escape(config_get_log_level(config), escaped_level, sizeof(escaped_level));
    web_server_json_escape(config_get_log_file(config), escaped_file, sizeof(escaped_file));
    snprintf(json, sizeof(json),
        "{"
        "\"hardware\":{"
        "\"display_device\":\"%s\","
        "\"baud_rate\":%d"
        "},"
        "\"call\":{"
        "\"cost_cents\":%d,"
        "\"timeout_seconds\":%d"
        "},"
        "\"logging\":{"
        "\"level\":\"%s\","
        "\"file\":\"%s\","
        "\"to_file\":%s"
        "},"
        "\"system\":{"
        "\"update_interval_ms\":%d,"
        "\"max_retries\":%d"
        "},"
        "\"metrics_server\":{"
        "\"enabled\":%s,"
        "\"port\":%d"
        "},"
        "\"web_server\":{"
        "\"enabled\":%s,"
        "\"port\":%d"
        "}"
        "}",
        escaped_device,
        config_get_baud_rate(config),
        config_get_call_cost_cents(config),
        config_get_call_timeout_seconds(config),
        escaped_level,
        escaped_file,
        config_get_log_to_file(config) ? "true" : "false",
        config_get_update_interval_ms(config),
        config_get_max_retries(config),
        config_get_metrics_server_enabled(config) ? "true" : "false",
        config_get_metrics_server_port(config),
        config_get_web_server_enabled(config) ? "true" : "false",
        config_get_web_server_port(config));
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_state(const struct http_request* request) {
    struct http_response response;
    char json[1024];
    struct daemon_state_info state_info;
    char line1[64], line2[64];
    char escaped_line1[128], escaped_line2[128];
    char escaped_keypad[128];
    char escaped_error[256];
    (void)request; /* Suppress unused parameter warning */
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    state_info = get_daemon_state_info();

    /* Escape strings for JSON (prevent injection from keypad_buffer, sip_last_error) */
    web_server_json_escape(state_info.keypad_buffer, escaped_keypad, sizeof(escaped_keypad));
    web_server_json_escape(state_info.sip_last_error, escaped_error, sizeof(escaped_error));

    /* Current VFD text, so the dashboard can show what the phone is displaying
     * (e.g. the active game) on initial load. */
    display_manager_get_text(line1, sizeof(line1), line2, sizeof(line2));
    web_server_json_escape(line1, escaped_line1, sizeof(escaped_line1));
    web_server_json_escape(line2, escaped_line2, sizeof(escaped_line2));

    snprintf(json, sizeof(json),
        "{"
        "\"current_state\":%d,"
        "\"inserted_cents\":%d,"
        "\"keypad_buffer\":\"%s\","
        "\"last_activity\":%ld,"
        "\"sip_registered\":%d,"
        "\"sip_last_error\":\"%s\","
        "\"line1\":\"%s\",\"line2\":\"%s\""
        "}",
        state_info.current_state,
        state_info.inserted_cents,
        escaped_keypad,
        state_info.last_activity,
        state_info.sip_registered,
        escaped_error,
        escaped_line1, escaped_line2);
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_control(const struct http_request* request) {
    struct http_response response;
    char json[1024];
    char action[64] = "unknown";
    char key[16] = "";
    char plugin[64] = "";
    int cents = 0;
    char* action_pos;
    char* key_pos;
    char* plugin_pos;
    int success = 0;
    char message[256] = "Unknown action";
    char escaped_action[128];
    char escaped_message[512];
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    /* Parse action from JSON body */
    action_pos = strstr(request->body, "\"action\":\"");
    if (action_pos) {
        char* start = action_pos + 10; /* Length of "action":" */
        char* end = strchr(start, '"');
        if (end) {
            size_t len = end - start;
            if (len < sizeof(action)) {
                memcpy(action, start, len);
                action[len] = '\0';
            }
        }
    }
    
    /* Parse key for keypad actions (supports "key" or "arg") */
    key_pos = strstr(request->body, "\"key\":\"");
    if (key_pos) {
        char* start = key_pos + 7; /* Length of "key":" */
        char* end = strchr(start, '"');
        if (end) {
            size_t len = end - start;
            if (len < sizeof(key)) {
                memcpy(key, start, len);
                key[len] = '\0';
            }
        }
    } else {
        char* arg_pos = strstr(request->body, "\"arg\":\"");
        if (arg_pos && key[0] == '\0') {
            char* start = arg_pos + 7; /* Length of "arg":" */
            char* end = strchr(start, '"');
            if (end) {
                size_t len = end - start;
                if (len > 0 && len < sizeof(key)) {
                    memcpy(key, start, len);
                    key[len] = '\0';
                }
            }
        }
    }
    
    /* Parse cents for coin_insert (supports "cents" or "arg") - use strtol to avoid
     * atoi overflow/undefined behavior (#129) */
    {
        char* parse_start = NULL;
        char* cents_pos = strstr(request->body, "\"cents\":");
        if (cents_pos) {
            parse_start = cents_pos + 8; /* Length of "cents": */
        } else {
            char* arg_quoted = strstr(request->body, "\"arg\":\"");
            char* arg_num = strstr(request->body, "\"arg\":");
            if (arg_quoted)
                parse_start = arg_quoted + 7;
            else if (arg_num)
                parse_start = arg_num + 6;
        }
        if (parse_start) {
            char* endptr;
            long val = strtol(parse_start, &endptr, 10);
            if (endptr != parse_start && val >= 1 && val <= 999 && val <= 0x7FFFFFFF) {
                cents = (int)val;
            }
        }
    }
    
    /* Parse plugin for plugin actions */
    plugin_pos = strstr(request->body, "\"plugin\":\"");
    if (plugin_pos) {
        char* start = plugin_pos + 10; /* Length of "plugin":" */
        char* end = strchr(start, '"');
        if (end) {
            size_t len = end - start;
            if (len < sizeof(plugin)) {
                memcpy(plugin, start, len);
                plugin[len] = '\0';
            }
        }
    }
    
    /* Handle different actions */
    if (strcmp(action, "start_call") == 0) {
        success = send_control_command("start_call");
        snprintf(message, sizeof(message), "%s", success ? "Call initiation requested" : "Failed to initiate call");
    } else if (strcmp(action, "reset_system") == 0) {
        success = send_control_command("reset_system");
        snprintf(message, sizeof(message), "%s", success ? "System reset initiated" : "Failed to reset system");
    } else if (strcmp(action, "emergency_stop") == 0) {
        success = send_control_command("emergency_stop");
        snprintf(message, sizeof(message), "%s", success ? "Emergency stop activated" : "Failed to activate emergency stop");
    } else if (strcmp(action, "keypad_press") == 0) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "keypad_press:%s", key);
        success = send_control_command(cmd);
        snprintf(message, sizeof(message), success ? "Keypad key %s pressed" : "Failed to simulate keypad press", key);
    } else if (strcmp(action, "keypad_clear") == 0) {
        success = send_control_command("keypad_clear");
        snprintf(message, sizeof(message), "%s", success ? "Keypad cleared" : "Failed to clear keypad");
    } else if (strcmp(action, "keypad_backspace") == 0) {
        success = send_control_command("keypad_backspace");
        snprintf(message, sizeof(message), "%s", success ? "Keypad backspace" : "Failed to simulate backspace");
    } else if (strcmp(action, "coin_insert") == 0) {
        if (cents < 1 || cents > 999) {
            snprintf(message, sizeof(message), "Invalid cents: must be 1-999");
        } else {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "coin_insert:%d", cents);
            success = send_control_command(cmd);
            snprintf(message, sizeof(message), success ? "Coin inserted: %d¢" : "Failed to insert coin", cents);
        }
    } else if (strcmp(action, "coin_return") == 0) {
        success = send_control_command("coin_return");
        snprintf(message, sizeof(message), "%s", success ? "Coins returned" : "Failed to return coins");
    } else if (strcmp(action, "handset_up") == 0) {
        success = send_control_command("handset_up");
        snprintf(message, sizeof(message), "%s", success ? "Handset lifted" : "Failed to simulate handset up");
    } else if (strcmp(action, "handset_down") == 0) {
        success = send_control_command("handset_down");
        snprintf(message, sizeof(message), "%s", success ? "Handset placed down" : "Failed to simulate handset down");
    } else if (strcmp(action, "activate_plugin") == 0) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "activate_plugin:%s", plugin);
        success = send_control_command(cmd);
        if (success) {
            snprintf(message, sizeof(message), "Plugin %s activated", plugin);
        } else {
            snprintf(message, sizeof(message), "Failed to activate plugin %s", plugin);
        }
    } else {
        snprintf(message, sizeof(message), "Unknown action: %s", action);
    }
    
    /* Escape for JSON to prevent injection when action/message contain " or \ */
    web_server_json_escape(action, escaped_action, sizeof(escaped_action));
    web_server_json_escape(message, escaped_message, sizeof(escaped_message));
    
    snprintf(json, sizeof(json),
        "{"
        "\"success\":%s,"
        "\"action\":\"%s\","
        "\"message\":\"%s\""
        "}",
        success ? "true" : "false",
        escaped_action,
        escaped_message);
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_logs(const struct http_request* request) {
    struct http_response response;
    char level[16] = "INFO";
    int i;
    int max_entries = 20;
    char json[8192];
    char* ptr = json;
    size_t remaining = sizeof(json);
    int written;
    log_level_t min_level = LOG_LEVEL_INFO;
    char log_buffer[50][512];
    int log_count;
    int first = 1;
    int total_count = 0;
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    /* Get log level parameter */
    for (i = 0; i < request->query_count; i++) {
        if (strcmp(request->query_keys[i], "level") == 0) {
            web_server_strcpy_safe(level, request->query_values[i], sizeof(level));
            break;
        }
    }

    /* Get max entries parameter (default to 20, max 50) */
    for (i = 0; i < request->query_count; i++) {
        if (strcmp(request->query_keys[i], "max_entries") == 0) {
            max_entries = atoi(request->query_values[i]);
            if (max_entries > 50) max_entries = 50;
            if (max_entries < 1) max_entries = 1;
            break;
        }
    }

    written = snprintf(ptr, remaining, "{\"logs\":[");
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }

    /* Map requested level name -> minimum severity. This is a THRESHOLD, not an
     * exact match, so an INFO view still surfaces WARN/ERROR. */
    if (strcmp(level, "ALL") == 0 || strcmp(level, "VERBOSE") == 0) min_level = LOG_LEVEL_VERBOSE;
    else if (strcmp(level, "DEBUG") == 0) min_level = LOG_LEVEL_DEBUG;
    else if (strcmp(level, "INFO") == 0) min_level = LOG_LEVEL_INFO;
    else if (strcmp(level, "WARN") == 0 || strcmp(level, "WARNING") == 0) min_level = LOG_LEVEL_WARN;
    else if (strcmp(level, "ERROR") == 0) min_level = LOG_LEVEL_ERROR;

    /* Get logs from memory, level-filtered across the whole ring buffer so
     * INFO+ events appear even when DEBUG output dominates the recent window. */
    log_count = logger_get_recent_logs_min_level(log_buffer, max_entries, min_level);

    /* Process logs in reverse order (newest first) */
    for (i = log_count - 1; i >= 0 && remaining > 200; i--) {
        char escaped_message[512];
        time_t log_timestamp;
        char log_level[16] = "INFO"; /* Default level */
        char* timestamp_start;
        char escaped_level[32];
        if (!first) {
            written = snprintf(ptr, remaining, ",");
            if (written > 0 && (size_t)written < remaining) {
                ptr += written;
                remaining -= written;
            }
        }
        
        /* JSON-escape log message: ", \, and control chars (#112) */
        {
            char *src = log_buffer[i];
            char *dst = escaped_message;
            size_t dst_remaining = sizeof(escaped_message) - 1;
            /* Skip the "[timestamp] [LEVEL] " prefix so the message field holds
             * just "[category] text" (the client renders its own time + level). */
            { char *b1 = strchr(src, ']');
              if (b1) { char *b2 = strchr(b1 + 1, ']'); if (b2 && b2[1] == ' ') src = b2 + 2; } }
            while (*src && dst_remaining > 2) {
                if (*src == '"' || *src == '\\') {
                    *dst++ = '\\';
                    *dst++ = *src;
                    dst_remaining -= 2;
                } else if (*src == '\n' || *src == '\r' || (unsigned char)*src < 32) {
                    *dst++ = ' ';
                    dst_remaining--;
                } else {
                    *dst++ = *src;
                    dst_remaining--;
                }
                src++;
            }
            *dst = '\0';
            if (strlen(escaped_message) > 400) {
                escaped_message[397] = '.';
                escaped_message[398] = '.';
                escaped_message[399] = '.';
                escaped_message[400] = '\0';
            }
        }
        
        /* Extract timestamp and level from the log message */
        log_timestamp = time(NULL); /* Default to current time */

        /* Try to parse timestamp from log message format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [CATEGORY] message */
        timestamp_start = strstr(log_buffer[i], "[");
        if (timestamp_start) {
            char* timestamp_end = strstr(timestamp_start + 1, "]");
            if (timestamp_end) {
                /* Parse the timestamp - format: YYYY-MM-DD HH:MM:SS.mmm */
                int year, month, day, hour, min, sec, msec;
                char* level_start;
                if (sscanf(timestamp_start + 1, "%d-%d-%d %d:%d:%d.%d",
                          &year, &month, &day, &hour, &min, &sec, &msec) == 7) {
                    /* Convert to time_t (approximate) */
                    struct tm tm_time = {0};
                    tm_time.tm_year = year - 1900;
                    tm_time.tm_mon = month - 1;
                    tm_time.tm_mday = day;
                    tm_time.tm_hour = hour;
                    tm_time.tm_min = min;
                    tm_time.tm_sec = sec;
                    log_timestamp = mktime(&tm_time);
                }
                
                /* Try to parse log level from the next [LEVEL] section */
                level_start = strstr(timestamp_end + 1, "[");
                if (level_start) {
                    char* level_end = strstr(level_start + 1, "]");
                    if (level_end) {
                        size_t level_len = level_end - level_start - 1;
                        if (level_len > 0 && level_len < sizeof(log_level) - 1) {
                            strncpy(log_level, level_start + 1, level_len);
                            log_level[level_len] = '\0';
                        }
                    }
                }
            }
        }
        
        /* Level filtering already applied by logger_get_recent_logs_min_level. */

        /* Escape log_level for JSON (parsed from log, could have special chars) */
        web_server_json_escape(log_level, escaped_level, sizeof(escaped_level));
        written = snprintf(ptr, remaining,
            "{\"timestamp\":%ld,\"level\":\"%s\",\"message\":\"%s\"}",
            log_timestamp, escaped_level, escaped_message);
        if (written > 0 && (size_t)written < remaining) {
            ptr += written;
            remaining -= written;
        }
        
        first = 0;
        total_count++;
    }
    
    written = snprintf(ptr, remaining, "],\"total\":%d}", total_count);
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_plugins(const struct http_request* request) {
    struct http_response response;
    char json[2048];
    (void)request; /* Suppress unused parameter warning */
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    /* Enumerate the registry dynamically so newly added plugins appear in the
     * dashboard automatically (no hard-coded list to keep in sync). */
    if (plugins_to_json(json, sizeof(json)) < 0) {
        web_server_strcpy_safe(json, "{\"plugins\":[],\"active_plugin\":\"\"}",
                               sizeof(json));
    }

    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_version(const struct http_request* request) {
    struct http_response response;
    char json[512];
    (void)request;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    response.status_code = 200;
    snprintf(json, sizeof(json),
        "{\"version\":\"%s\",\"git_hash\":\"%s\",\"build_time\":\"%s\"}",
        version_get_string(), version_get_git_hash(), version_get_build_time());
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_check_update(const struct http_request* request) {
    struct http_response response;
    char json[512];
    const char *latest;
    (void)request;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    response.status_code = 200;

    /* #119: Non-blocking - don't block HTTP handler for curl timeout */
    updater_check_async();
    latest = updater_get_latest_version();

    snprintf(json, sizeof(json),
        "{\"current_version\":\"%s\",\"latest_version\":\"%s\","
        "\"update_available\":%s,\"checking\":%s,\"git_hash\":\"%s\"}",
        version_get_string(),
        latest ? latest : (updater_is_checking() ? "" : "unknown"),
        updater_is_update_available() ? "true" : "false",
        updater_is_checking() ? "true" : "false",
        version_get_git_hash());
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_update(const struct http_request* request) {
    struct http_response response;
    char json[512];
    const char *source_dir;
    int rc;
    (void)request;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));

    /* #118: Non-blocking - don't block web server for minutes */
    source_dir = config_get_string(config_get_instance(), "system.source_dir", "/home/matzen/millennium");
    rc = updater_apply_async(source_dir);

    if (rc == 0 && updater_is_applying()) {
        snprintf(json, sizeof(json),
            "{\"success\":true,\"status\":\"Applying update in background. Daemon will restart when complete.\",\"accepted\":true}");
        response.status_code = 202;  /* Accepted */
    } else {
        snprintf(json, sizeof(json),
            "{\"success\":%s,\"status\":\"%s\"}",
            rc == 0 ? "true" : "false",
            updater_get_apply_status());
        response.status_code = rc == 0 ? 200 : 500;
    }
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_dashboard(const struct http_request* request) {
    struct http_response response;
    (void)request;
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    web_server_strcpy_safe(response.content_type, "text/html", sizeof(response.content_type));

    snprintf(response.body, sizeof(response.body),
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Millennium Payphone</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:-apple-system,system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;min-height:100vh}"
        ".header{background:#16213e;padding:16px 24px;border-bottom:1px solid #0f3460;display:flex;justify-content:space-between;align-items:center}"
        ".header h1{font-size:20px;color:#e94560}"
        ".ver{font-size:12px;color:#888}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px;padding:20px}"
        ".card{background:#16213e;border-radius:8px;padding:20px;border:1px solid #0f3460}"
        ".card h2{font-size:14px;color:#e94560;margin-bottom:12px;text-transform:uppercase;letter-spacing:1px}"
        ".state{font-size:28px;font-weight:700;color:#53d769;margin:8px 0}"
        ".display-box{background:#0a0a1a;border:1px solid #333;border-radius:4px;padding:12px;font-family:'Courier New',monospace;font-size:16px;color:#0f0;min-height:56px;margin:8px 0}"
        ".metric{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #0f3460}"
        ".metric:last-child{border:none}"
        ".metric .label{color:#888}.metric .value{font-weight:600}"
        ".btn{background:#e94560;color:#fff;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;font-size:13px;margin:4px}"
        ".btn:hover{background:#c73652}"
        ".btn.secondary{background:#0f3460}.btn.secondary:hover{background:#16213e}"
        ".status-dot{display:inline-block;width:8px;height:8px;border-radius:50%%;margin-right:6px}"
        ".dot-green{background:#53d769}.dot-yellow{background:#f5a623}.dot-red{background:#e94560}"
        "#log{background:#0a0a1a;border:1px solid #333;border-radius:4px;padding:8px;font-family:monospace;font-size:11px;color:#aaa;max-height:200px;overflow-y:auto}"
        "</style></head><body>"

        "<div class=\"header\"><h1>Millennium Payphone</h1>"
        "<span class=\"ver\" id=\"ver\">v%s (%s)</span></div>"

        "<div class=\"grid\">"

        "<div class=\"card\"><h2>Phone State</h2>"
        "<div class=\"state\" id=\"state\">Connecting...</div>"
        "<div class=\"display-box\" id=\"vfd\">-- -- -- -- -- -- -- --</div>"
        "<div class=\"metric\"><span class=\"label\">Coins</span><span class=\"value\" id=\"coins\">--</span></div>"
        "<div class=\"metric\"><span class=\"label\">Keypad</span><span class=\"value\" id=\"keypad\">--</span></div>"
        "<div class=\"metric\"><span class=\"label\">Plugin</span><span class=\"value\" id=\"plugin\">--</span></div>"
        "</div>"

        "<div class=\"card\"><h2>Plugins</h2>"
        "<div id=\"plugins\" style=\"display:flex;gap:6px;flex-wrap:wrap\">"
        "<span style=\"font-size:12px;color:#888\">Loading...</span>"
        "</div></div>"

        "<div class=\"card\"><h2>Play</h2>"
        "<div style=\"display:flex;gap:6px;margin-bottom:8px\">"
        "<button class=\"btn\" onclick=\"hook(1)\">Lift</button>"
        "<button class=\"btn secondary\" onclick=\"hook(0)\">Hang Up</button>"
        "</div>"
        "<div style=\"display:flex;gap:6px;margin-bottom:8px\">"
        "<button class=\"btn\" onclick=\"coin(5)\">5&cent;</button>"
        "<button class=\"btn\" onclick=\"coin(10)\">10&cent;</button>"
        "<button class=\"btn\" onclick=\"coin(25)\">25&cent;</button>"
        "<button class=\"btn secondary\" onclick=\"post('/api/control',{action:'coin_return'})\">Return</button>"
        "</div>"
        "<div style=\"display:grid;grid-template-columns:repeat(3,1fr);gap:6px;max-width:220px\">"
        "<button class=\"btn\" onclick=\"k('1')\">1</button>"
        "<button class=\"btn\" onclick=\"k('2')\">2</button>"
        "<button class=\"btn\" onclick=\"k('3')\">3</button>"
        "<button class=\"btn\" onclick=\"k('4')\">4</button>"
        "<button class=\"btn\" onclick=\"k('5')\">5</button>"
        "<button class=\"btn\" onclick=\"k('6')\">6</button>"
        "<button class=\"btn\" onclick=\"k('7')\">7</button>"
        "<button class=\"btn\" onclick=\"k('8')\">8</button>"
        "<button class=\"btn\" onclick=\"k('9')\">9</button>"
        "<button class=\"btn secondary\" onclick=\"k('*')\">*</button>"
        "<button class=\"btn\" onclick=\"k('0')\">0</button>"
        "<button class=\"btn secondary\" onclick=\"k('#')\">#</button>"
        "</div></div>"

        "<div class=\"card\"><h2>System</h2>"
        "<button class=\"btn\" onclick=\"api('/api/check-update').then(d=>{"
        "var s=d.update_available?'Update: '+d.latest_version:'Up to date';"
        "document.getElementById('upd').textContent=s;"
        "document.getElementById('apply-btn').style.display=d.update_available?'inline-block':'none';"
        "})\">Check Updates</button>"
        "<button id=\"apply-btn\" class=\"btn\" style=\"display:none;background:#53d769\" "
        "onclick=\"if(confirm('Apply update and restart daemon?')){"
        "document.getElementById('upd').textContent='Updating...';"
        "post('/api/update',{}).then(d=>document.getElementById('upd').textContent=d.status)"
        ".catch(()=>document.getElementById('upd').textContent='Connection lost (restarting)');"
        "}\">Apply Update</button>"
        "<span id=\"upd\" style=\"font-size:12px;margin-left:8px\"></span>"
        "<hr style=\"border-color:#0f3460;margin:12px 0\">"
        "<button class=\"btn secondary\" onclick=\"api('/api/health').then(d=>alert(JSON.stringify(d,null,2)))\">Health</button>"
        "<button class=\"btn secondary\" onclick=\"api('/api/metrics').then(d=>alert(JSON.stringify(d,null,2)))\">Metrics</button>"
        "</div>"

        "<div class=\"card\"><h2>Live Events</h2>"
        "<div id=\"log\"></div></div>"

        "</div>"

        "<script>"
        "function api(u){return fetch(u).then(r=>r.json())}"
        "function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(r=>r.json())}"
        "function k(x){post('/api/control',{action:'keypad_press',key:x})}"
        "function coin(c){post('/api/control',{action:'coin_insert',cents:c})}"
        "function hook(u){post('/api/control',{action:u?'handset_up':'handset_down'})}"
        "function vfd(a,b){var e=document.getElementById('vfd');if(e)e.textContent=((a||'')+'                    ').slice(0,20)+' | '+((b||'')+'                    ').slice(0,20)}"
        "var ws,log=document.getElementById('log');"
        "function connect(){"
        "ws=new WebSocket('ws://'+location.host+'/ws');"
        "ws.onmessage=function(e){"
        "try{var d=JSON.parse(e.data);"
        "if(d.state)document.getElementById('state').textContent=d.state;"
        "if(d.coins!==undefined)document.getElementById('coins').textContent=d.coins+'c';"
        "if(d.keypad!==undefined)document.getElementById('keypad').textContent=d.keypad||'(empty)';"
        "if(d.plugin)document.getElementById('plugin').textContent=d.plugin;"
        "if(d.line1!==undefined||d.line2!==undefined)vfd(d.line1,d.line2);"
        "var p=document.createElement('div');"
        "p.textContent=new Date().toLocaleTimeString()+' '+d.event;"
        "log.prepend(p);"
        "if(log.children.length>50)log.lastChild.remove();"
        "}catch(x){}};"
        "ws.onclose=function(){setTimeout(connect,2000)};"
        "}"
        "connect();"
        "function loadPlugins(){"
        "api('/api/plugins').then(function(d){"
        "var c=document.getElementById('plugins');c.innerHTML='';"
        "(d.plugins||[]).forEach(function(p){"
        "var b=document.createElement('button');"
        "b.className='btn'+(p.active?'':' secondary');"
        "b.textContent=p.name;b.title=p.description||'';"
        "b.onclick=function(){post('/api/control',{action:'activate_plugin',plugin:p.name}).then(loadPlugins)};"
        "c.appendChild(b);"
        "});"
        "if(!(d.plugins||[]).length)c.innerHTML='<span style=\"font-size:12px;color:#888\">No plugins</span>';"
        "}).catch(function(){});"
        "}"
        "loadPlugins();"
        "api('/api/state').then(function(d){"
        "document.getElementById('state').textContent=d.state||'Unknown';"
        "document.getElementById('coins').textContent=(d.inserted_cents||0)+'c';"
        "document.getElementById('keypad').textContent=d.keypad_buffer||'(empty)';"
        "vfd(d.line1,d.line2);"
        "}).catch(function(){});"
        "</script>"

        "</body></html>",
        version_get_string(), version_get_git_hash());

    return response;
}

/* Utility functions */
int web_server_is_in_call(void) {
    struct daemon_state_info state = get_daemon_state_info();
    /* States: 0=INVALID, 1=IDLE_DOWN, 2=IDLE_UP, 3=CALL_INCOMING, 4=CALL_ACTIVE */
    return state.current_state >= 3; /* CALL_INCOMING or CALL_ACTIVE */
}

int web_server_is_ringing(void) {
    struct daemon_state_info state = get_daemon_state_info();
    /* State 3 = CALL_INCOMING (ringing) */
    return state.current_state == 3;
}

int web_server_is_high_priority_state(void) {
    struct daemon_state_info state = get_daemon_state_info();
    /* High priority states: ringing (3) or active call (4) */
    return state.current_state >= 3;
}

int web_server_is_audio_active(void) {
    struct daemon_state_info state = get_daemon_state_info();
    /* Audio is active during ringing (3) or active call (4) */
    /* Also consider IDLE_UP (2) as potentially audio-active since handset is up */
    return state.current_state >= 2;
}

int web_server_check_rate_limit(struct web_server* server, const char* client_ip, const char* endpoint) {
    time_t now;
    char key[128];
    int i;
    int found_idx = -1;
    if (!server || !client_ip || !endpoint) return 1;

    now = time(NULL);
    snprintf(key, sizeof(key), "%s:%s", client_ip, endpoint);

    /* The rate-limit table is shared across worker threads. */
    pthread_mutex_lock(&server->state_mutex);

    /* Clean up old entries (older than 60 seconds) */
    for (i = 0; i < server->rate_limit_count; i++) {
        if (now - server->rate_limit_infos[i].last_request > 60) {
            /* Remove this entry by shifting remaining entries */
            int j;
            for (j = i; j < server->rate_limit_count - 1; j++) {
                strncpy(server->rate_limit_keys[j], server->rate_limit_keys[j + 1], sizeof(server->rate_limit_keys[j]) - 1);
                server->rate_limit_keys[j][sizeof(server->rate_limit_keys[j]) - 1] = '\0';
                server->rate_limit_infos[j] = server->rate_limit_infos[j + 1];
            }
            server->rate_limit_count--;
            i--; /* Check this index again */
        }
    }
    
    /* Find existing entry or create new one */
    for (i = 0; i < server->rate_limit_count; i++) {
        if (strcmp(server->rate_limit_keys[i], key) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx == -1) {
        /* Create new entry */
        if (server->rate_limit_count < 64) {
            found_idx = server->rate_limit_count;
            web_server_strcpy_safe(server->rate_limit_keys[found_idx], key,
                                   sizeof(server->rate_limit_keys[found_idx]));
            server->rate_limit_infos[found_idx].request_count = 0;
            server->rate_limit_count++;
        } else {
            pthread_mutex_unlock(&server->state_mutex);
            return 1; /* Rate limit table full, allow request */
        }
    }

    /* Update counters */
    server->rate_limit_infos[found_idx].request_count++;
    server->rate_limit_infos[found_idx].last_request = now;

    pthread_mutex_unlock(&server->state_mutex);
    return 1; /* Allowed - simplified rate limiting for C89 */
}

struct http_response web_server_create_rate_limit_response(void) {
    struct http_response response;
    const char* json;
    memset(&response, 0, sizeof(response));
    response.status_code = 429; /* Too Many Requests */
    
    web_server_strcpy_safe(response.header_keys[response.header_count], "Retry-After", sizeof(response.header_keys[response.header_count]));
    web_server_strcpy_safe(response.header_values[response.header_count], "10", sizeof(response.header_values[response.header_count]));
    response.header_count++;
    
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    json =
        "{"
        "\"error\": \"Rate limit exceeded\","
        "\"message\": \"Too many requests. Please slow down, especially during calls.\","
        "\"retry_after\": 10"
        "}";
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

/* WebSocket support (simplified) */
void web_server_add_websocket_route(struct web_server* server, const char* path, websocket_handler_t handler) {
    if (!server || !path || !handler) return;
    /* WebSocket routes are handled specially in the request processing */
    server->websocket_handler = handler;
}

/* Remove a websocket fd from the connection list (matched by value) and close
 * it. Safe against the list shifting under concurrent registration because it
 * re-scans under the lock rather than trusting a cached index. */
static void web_server_drop_websocket(struct web_server* server, int fd) {
    int i;
    pthread_mutex_lock(&server->state_mutex);
    for (i = 0; i < server->websocket_count; i++) {
        if (server->websocket_connections[i] == fd) {
            int j;
            for (j = i; j < server->websocket_count - 1; j++) {
                server->websocket_connections[j] = server->websocket_connections[j + 1];
            }
            server->websocket_count--;
            break;
        }
    }
    pthread_mutex_unlock(&server->state_mutex);
    close(fd);
}

void web_server_broadcast_to_websockets(struct web_server* server, const char* message) {
    int fds[32];
    int n = 0;
    int i;
    if (!server || !message) return;

    /* Snapshot the connection list under the lock, then send outside it: a slow
     * or stalled websocket client must not block workers registering new ones.
     * Only this (single) broadcaster sends to or removes ws fds, so an fd can't
     * be double-closed. */
    pthread_mutex_lock(&server->state_mutex);
    for (i = 0; i < server->websocket_count && n < (int)(sizeof(fds) / sizeof(fds[0])); i++) {
        fds[n++] = server->websocket_connections[i];
    }
    pthread_mutex_unlock(&server->state_mutex);

    for (i = 0; i < n; i++) {
        if (ws_send_text(fds[i], message) != 0) {
            web_server_drop_websocket(server, fds[i]);
        }
    }
}

/* Streaming response implementation */
int web_server_send_streaming_response(int client_fd, const struct http_response* response) {
    char headers[1024];
    char* ptr = headers;
    size_t remaining = sizeof(headers);
    int written;
    int i;
    FILE* file;
    char buffer[8192];  /* 8KB buffer for efficient reading */
    size_t bytes_read;
    int total_sent = 0;
    if (!response || !response->is_streaming) return -1;

    /* Send HTTP headers first */

    /* Status line */
    written = snprintf(ptr, remaining, "HTTP/1.1 %d OK\r\n", response->status_code);
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Content-Type header */
    written = snprintf(ptr, remaining, "Content-Type: %s\r\n", response->content_type);
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Content-Length header */
    written = snprintf(ptr, remaining, "Content-Length: %lu\r\n", (unsigned long)response->content_length);
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Additional headers */
    for (i = 0; i < response->header_count; i++) {
        written = snprintf(ptr, remaining, "%s: %s\r\n", 
                          response->header_keys[i], response->header_values[i]);
        if (written > 0 && (size_t)written < remaining) {
            ptr += written;
            remaining -= written;
        }
    }
    
    /* Connection header */
    written = snprintf(ptr, remaining, "Connection: close\r\n\r\n");
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Send headers */
    if (send(client_fd, headers, strlen(headers), 0) < 0) {
        return -1;
    }
    
    /* Open and send file content */
    file = fopen(response->file_path, "rb");
    if (!file) {
        return -1;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        ssize_t sent = send(client_fd, buffer, bytes_read, 0);
        if (sent < 0) {
            fclose(file);
            return -1;
        }
        total_sent += sent;
    }
    
    fclose(file);
    return total_sent;
}
