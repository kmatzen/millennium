#define _POSIX_C_SOURCE 200112L
#include "web_server.h"
#include "config.h"
#include "logger.h"
#include "metrics.h"
#include "health_monitor.h"
#include "plugins.h"

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

/* String utility functions */
void web_server_strcpy_safe(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    size_t i;
    for (i = 0; i < dest_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

void web_server_strcat_safe(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    size_t dest_len = web_server_strlen_safe(dest);
    if (dest_len >= dest_size - 1) return;
    
    size_t i;
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
    
    /* Initialize static route flags */
    int i;
    for (i = 0; i < 16; i++) {
        server->static_is_file[i] = 0;
    }
    
    return server;
}

void web_server_destroy(struct web_server* server) {
    if (!server) return;
    
    web_server_stop(server);
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
    
    /* Start server thread */
    if (pthread_create(&server->server_thread, NULL, web_server_thread_func, server) != 0) {
        logger_error_with_category("WebServer", "Failed to create server thread");
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
    
    /* Wait for thread to finish */
    pthread_join(server->server_thread, NULL);
    
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
    if (!server || !method || !path || !handler) return;
    if (server->route_count >= 32) return; /* Max routes reached */
    
    int idx = server->route_count;
    web_server_strcpy_safe(server->route_methods[idx], method, sizeof(server->route_methods[idx]));
    web_server_strcpy_safe(server->route_paths[idx], path, sizeof(server->route_paths[idx]));
    server->route_handlers[idx] = handler;
    server->route_count++;
}

void web_server_add_static_route(struct web_server* server, const char* path, const char* content, const char* content_type) {
    if (!server || !path || !content || !content_type) return;
    if (server->static_count >= 16) return; /* Max static routes reached */
    
    int idx = server->static_count;
    web_server_strcpy_safe(server->static_paths[idx], path, sizeof(server->static_paths[idx]));
    web_server_strcpy_safe(server->static_contents[idx], content, sizeof(server->static_contents[idx]));
    web_server_strcpy_safe(server->static_content_types[idx], content_type, sizeof(server->static_content_types[idx]));
    server->static_is_file[idx] = 0;  /* Using content, not file */
    server->static_count++;
}

void web_server_add_file_route(struct web_server* server, const char* path, const char* file_path, const char* content_type) {
    if (!server || !path || !file_path || !content_type) return;
    if (server->static_count >= 16) return; /* Max static routes reached */
    
    int idx = server->static_count;
    web_server_strcpy_safe(server->static_paths[idx], path, sizeof(server->static_paths[idx]));
    web_server_strcpy_safe(server->static_file_paths[idx], file_path, sizeof(server->static_file_paths[idx]));
    web_server_strcpy_safe(server->static_content_types[idx], content_type, sizeof(server->static_content_types[idx]));
    server->static_is_file[idx] = 1;  /* Using file path */
    server->static_count++;
}

/* HTTP parsing functions */
struct http_request web_server_parse_request(const char* raw_request) {
    struct http_request request;
    memset(&request, 0, sizeof(request));
    
    if (!raw_request) return request;
    
    /* Parse request line */
    const char* line_start = raw_request;
    const char* line_end = strchr(line_start, '\n');
    if (!line_end) return request;
    
    /* Extract method and path */
    char request_line[512];
    size_t line_len = line_end - line_start;
    if (line_len >= sizeof(request_line)) line_len = sizeof(request_line) - 1;
    
    memcpy(request_line, line_start, line_len);
    request_line[line_len] = '\0';
    
    /* Remove \r if present */
    char* cr_pos = strchr(request_line, '\r');
    if (cr_pos) *cr_pos = '\0';
    
    /* Parse method and path */
    if (sscanf(request_line, "%15s %255s", request.method, request.path) != 2) {
        return request;
    }
    
    /* Remove query parameters from path */
    char* query_pos = strchr(request.path, '?');
    if (query_pos) {
        *query_pos = '\0';
        web_server_parse_query_string(query_pos + 1, &request);
    }
    
    /* Parse headers */
    const char* header_start = line_end + 1;
    while (header_start && *header_start != '\0') {
        const char* header_end = strchr(header_start, '\n');
        if (!header_end) break;
        
        /* Check for end of headers (empty line) */
        if (header_end == header_start + 1) {
            break;
        }
        
        char header_line[512];
        size_t header_len = header_end - header_start;
        if (header_len >= sizeof(header_line)) header_len = sizeof(header_line) - 1;
        
        memcpy(header_line, header_start, header_len);
        header_line[header_len] = '\0';
        
        /* Remove \r if present */
        char* cr_pos = strchr(header_line, '\r');
        if (cr_pos) *cr_pos = '\0';
        
        /* Parse header key:value */
        char* colon_pos = strchr(header_line, ':');
        if (colon_pos && request.header_count < 32) {
            *colon_pos = '\0';
            char* key = header_line;
            char* value = colon_pos + 1;
            
            /* Trim whitespace */
            while (*key == ' ' || *key == '\t') key++;
            while (*value == ' ' || *value == '\t') value++;
            
            char* key_end = key + strlen(key) - 1;
            while (key_end > key && (*key_end == ' ' || *key_end == '\t' || *key_end == '\r')) {
                *key_end = '\0';
                key_end--;
            }
            
            char* value_end = value + strlen(value) - 1;
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
    if (!query || !request) return;
    
    const char* start = query;
    while (*start && request->query_count < 16) {
        const char* end = strchr(start, '&');
        if (!end) end = start + strlen(start);
        
        char pair[256];
        size_t pair_len = end - start;
        if (pair_len >= sizeof(pair)) pair_len = sizeof(pair) - 1;
        
        memcpy(pair, start, pair_len);
        pair[pair_len] = '\0';
        
        char* eq_pos = strchr(pair, '=');
        if (eq_pos) {
            *eq_pos = '\0';
            char* key = pair;
            char* value = eq_pos + 1;
            
            /* URL decode */
            char decoded_key[64];
            char decoded_value[256];
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
    if (!str || !result || result_size == 0) return NULL;
    
    size_t i, j = 0;
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
    if (!server) return;
    
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        logger_error_with_category("WebServer", "Failed to create socket");
        return;
    }
    
    /* Set socket options for reuse */
    int opt = 1;
    setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
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
    int flags = fcntl(server->server_fd, F_GETFL, 0);
    fcntl(server->server_fd, F_SETFL, flags | O_NONBLOCK);
    
    char start_msg[256];
    snprintf(start_msg, sizeof(start_msg), "Web server started on port %d", server->port);
    logger_info_with_category("WebServer", start_msg);
}

/* Thread function for web server */
void* web_server_thread_func(void* arg) {
    struct web_server* server = (struct web_server*)arg;
    if (!server) return NULL;
    
    /* Main server loop - runs in separate thread */
    while (!server->should_stop) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server->server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd >= 0) {
            /* Handle request in a simple way (no threading to save resources) */
            web_server_handle_client(server, client_fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            /* Log error if it's not just a non-blocking accept */
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Accept failed with error: %d", errno);
            logger_error_with_category("WebServer", error_msg);
        }
        
        /* Small sleep to prevent busy waiting */
        /* Use select() for C89-compatible microsecond sleep */
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000; /* 1ms */
        select(0, NULL, NULL, NULL, &tv);
    }
    
    return NULL;
}

void web_server_handle_client(struct web_server* server, int client_fd) {
    if (!server) {
        close(client_fd);
        return;
    }
    
    char buffer[4096] = {0};
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        struct http_request parsed_request = web_server_parse_request(buffer);
        
        /* Get client IP for rate limiting */
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        if (getpeername(client_fd, (struct sockaddr*)&client_addr, &client_len) == 0) {
            web_server_strcpy_safe(parsed_request.client_ip, inet_ntoa(client_addr.sin_addr), sizeof(parsed_request.client_ip));
        } else {
            web_server_strcpy_safe(parsed_request.client_ip, "unknown", sizeof(parsed_request.client_ip));
        }
        
        struct http_response response = web_server_process_request(server, &parsed_request);
        
        if (response.is_streaming) {
            /* Send streaming response */
            web_server_send_streaming_response(client_fd, &response);
        } else {
            /* Send regular response */
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
    memset(&response, 0, sizeof(response));
    response.status_code = 200;
    
    if (!server || !request) {
        response.status_code = 500;
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
        response.status_code = 101;
        web_server_strcpy_safe(response.header_keys[response.header_count], "Upgrade", sizeof(response.header_keys[response.header_count]));
        web_server_strcpy_safe(response.header_values[response.header_count], "websocket", sizeof(response.header_values[response.header_count]));
        response.header_count++;
        web_server_strcpy_safe(response.header_keys[response.header_count], "Connection", sizeof(response.header_keys[response.header_count]));
        web_server_strcpy_safe(response.header_values[response.header_count], "Upgrade", sizeof(response.header_values[response.header_count]));
        response.header_count++;
        return response;
    }
    
    /* Check static routes first */
    int i;
    for (i = 0; i < server->static_count; i++) {
        if (strcmp(server->static_paths[i], request->path) == 0) {
            if (server->static_is_file[i]) {
                /* File route - create streaming response */
                response.is_streaming = 1;
                web_server_strcpy_safe(response.file_path, server->static_file_paths[i], sizeof(response.file_path));
                web_server_strcpy_safe(response.content_type, server->static_content_types[i], sizeof(response.content_type));
                
                /* Get file size for Content-Length header */
                FILE* file = fopen(response.file_path, "rb");
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
    if (!request) return 0;
    
    int i;
    int has_upgrade = 0, has_connection = 0;
    
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
    if (!response) return NULL;
    
    /* Calculate total size needed */
    size_t total_size = 1024; /* Base size for status line and headers */
    total_size += strlen(response->body);
    total_size += response->header_count * 128; /* Space for headers */
    
    char* result = (char*)web_server_malloc(total_size);
    if (!result) return NULL;
    
    char* ptr = result;
    size_t remaining = total_size;
    
    /* Status line */
    const char* status_text = "OK";
    switch (response->status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 101: status_text = "Switching Protocols"; break;
        case 429: status_text = "Too Many Requests"; break;
        default: status_text = "Unknown"; break;
    }
    
    int written = snprintf(ptr, remaining, "HTTP/1.1 %d %s\r\n", response->status_code, status_text);
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Headers */
    int i;
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
    size_t body_len = strlen(response->body);
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
}

/* Default handlers */
struct http_response web_server_handle_not_found(const struct http_request* request) {
    (void)request; /* Suppress unused parameter warning */
    struct http_response response;
    memset(&response, 0, sizeof(response));
    response.status_code = 404;
    web_server_strcpy_safe(response.content_type, "text/html", sizeof(response.content_type));
    
    const char* html = 
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
    (void)request; /* Suppress unused parameter warning */
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    /* Calculate actual uptime */
    time_t now = time(NULL);
    time_t start_time = get_daemon_start_time();
    time_t uptime_seconds = now - start_time;
    
    char json[512];
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
    (void)request; /* Suppress unused parameter warning */
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    /* Use C89 metrics API - simplified version */
    char *json_data = metrics_export_json();
    if (json_data) {
        web_server_strcpy_safe(response.body, json_data, sizeof(response.body));
        free(json_data);
    } else {
        web_server_strcpy_safe(response.body, "{\"error\":\"Failed to export metrics\"}", sizeof(response.body));
    }
    return response;
}

struct http_response web_server_handle_api_health(const struct http_request* request) {
    (void)request; /* Suppress unused parameter warning */
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    health_status_t overall_status = health_monitor_get_overall_status();
    health_check_t checks[32];
    int checks_count = health_monitor_get_all_checks(checks, 32);
    
    char json[2048];
    char* ptr = json;
    size_t remaining = sizeof(json);
    
    int written = snprintf(ptr, remaining, "{\"overall_status\":\"%s\",\"checks\":{", 
                          health_monitor_status_to_string(overall_status));
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    int i;
    int first = 1;
    for (i = 0; i < checks_count && remaining > 100; i++) {
        if (!first) {
            written = snprintf(ptr, remaining, ",");
            if (written > 0 && (size_t)written < remaining) {
                ptr += written;
                remaining -= written;
            }
        }
        
        written = snprintf(ptr, remaining,
            "\"%s\":{\"status\":\"%s\",\"message\":\"%s\",\"last_check\":%ld}",
            checks[i].name,
            health_monitor_status_to_string(checks[i].last_status),
            checks[i].last_message,
            checks[i].last_check_time);
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
    (void)request; /* Suppress unused parameter warning */
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    config_data_t* config = config_get_instance();
    if (!config) {
        web_server_strcpy_safe(response.body, "{\"error\":\"Config not available\"}", sizeof(response.body));
        return response;
    }
    
    char json[2048];
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
        config_get_display_device(config),
        config_get_baud_rate(config),
        config_get_call_cost_cents(config),
        config_get_call_timeout_seconds(config),
        config_get_log_level(config),
        config_get_log_file(config),
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
    (void)request; /* Suppress unused parameter warning */
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    char json[512];
    struct daemon_state_info state_info = get_daemon_state_info();
    
    snprintf(json, sizeof(json),
        "{"
        "\"current_state\":%d,"
        "\"inserted_cents\":%d,"
        "\"keypad_buffer\":\"%s\","
        "\"last_activity\":%ld"
        "}",
        state_info.current_state,
        state_info.inserted_cents,
        state_info.keypad_buffer,
        state_info.last_activity);
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_control(const struct http_request* request) {
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    char json[512];
    char action[64] = "unknown";
    char key[16] = "";
    char plugin[64] = "";
    int cents = 0;
    
    /* Parse action from JSON body */
    char* action_pos = strstr(request->body, "\"action\":\"");
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
    
    /* Parse key for keypad actions */
    char* key_pos = strstr(request->body, "\"key\":\"");
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
    }
    
    /* Parse cents for card actions */
    char* cents_pos = strstr(request->body, "\"cents\":");
    if (cents_pos) {
        cents = atoi(cents_pos + 8); /* Length of "cents": */
    }
    
    /* Parse plugin for plugin actions */
    char* plugin_pos = strstr(request->body, "\"plugin\":\"");
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
    int success = 0;
    char message[256] = "Unknown action";
    
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
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "coin_insert:%d", cents);
        success = send_control_command(cmd);
        snprintf(message, sizeof(message), success ? "Coin inserted: %d¢" : "Failed to insert coin", cents);
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
    
    snprintf(json, sizeof(json),
        "{"
        "\"success\":%s,"
        "\"action\":\"%s\","
        "\"message\":\"%s\""
        "}",
        success ? "true" : "false",
        action,
        message);
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
    return response;
}

struct http_response web_server_handle_api_logs(const struct http_request* request) {
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    /* Get log level parameter */
    char level[16] = "INFO";
    int i;
    for (i = 0; i < request->query_count; i++) {
        if (strcmp(request->query_keys[i], "level") == 0) {
            web_server_strcpy_safe(level, request->query_values[i], sizeof(level));
            break;
        }
    }
    
    /* Get max entries parameter (default to 20, max 50) */
    int max_entries = 20;
    for (i = 0; i < request->query_count; i++) {
        if (strcmp(request->query_keys[i], "max_entries") == 0) {
            max_entries = atoi(request->query_values[i]);
            if (max_entries > 50) max_entries = 50;
            if (max_entries < 1) max_entries = 1;
            break;
        }
    }
    
    char json[8192];
    char* ptr = json;
    size_t remaining = sizeof(json);
    
    int written = snprintf(ptr, remaining, "{\"logs\":[");
    if (written > 0 && (size_t)written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    /* Get logs from memory */
    char log_buffer[50][512];
    int log_count = logger_get_recent_logs(log_buffer, max_entries);
    
    int first = 1;
    int total_count = 0;
    /* Process logs in reverse order (newest first) */
    for (i = log_count - 1; i >= 0 && remaining > 200; i--) {
        if (!first) {
            written = snprintf(ptr, remaining, ",");
            if (written > 0 && (size_t)written < remaining) {
                ptr += written;
                remaining -= written;
            }
        }
        
        /* Simple log parsing - just escape and include */
        char escaped_message[256];
        char* src = log_buffer[i];
        char* dst = escaped_message;
        size_t dst_remaining = sizeof(escaped_message) - 1;
        
        while (*src && dst_remaining > 0) {
            if (*src == '"') {
                *dst++ = '\'';
                dst_remaining--;
            } else if (*src == '\n' || *src == '\r') {
                *dst++ = ' ';
                dst_remaining--;
            } else {
                *dst++ = *src;
                dst_remaining--;
            }
            src++;
        }
        *dst = '\0';
        
        /* Truncate if too long */
        if (strlen(escaped_message) > 200) {
            escaped_message[197] = '.';
            escaped_message[198] = '.';
            escaped_message[199] = '.';
            escaped_message[200] = '\0';
        }
        
        /* Extract timestamp and level from the log message */
        time_t log_timestamp = time(NULL); /* Default to current time */
        char log_level[16] = "INFO"; /* Default level */
        
        /* Try to parse timestamp from log message format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [CATEGORY] message */
        char* timestamp_start = strstr(log_buffer[i], "[");
        if (timestamp_start) {
            char* timestamp_end = strstr(timestamp_start + 1, "]");
            if (timestamp_end) {
                /* Parse the timestamp - format: YYYY-MM-DD HH:MM:SS.mmm */
                int year, month, day, hour, min, sec, msec;
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
                char* level_start = strstr(timestamp_end + 1, "[");
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
        
        /* Filter by level if specified */
        if (strcmp(level, "ALL") != 0) {
            /* Convert both to uppercase for comparison */
            char upper_level[16];
            char upper_log_level[16];
            int j;
            
            /* Convert requested level to uppercase */
            for (j = 0; level[j] != '\0' && j < (int)(sizeof(upper_level) - 1); j++) {
                upper_level[j] = toupper(level[j]);
            }
            upper_level[j] = '\0';
            
            /* Convert log level to uppercase */
            for (j = 0; log_level[j] != '\0' && j < (int)(sizeof(upper_log_level) - 1); j++) {
                upper_log_level[j] = toupper(log_level[j]);
            }
            upper_log_level[j] = '\0';
            
            /* Skip if level doesn't match */
            if (strcmp(upper_level, upper_log_level) != 0) {
                continue;
            }
        }
        
        written = snprintf(ptr, remaining,
            "{\"timestamp\":%ld,\"level\":\"%s\",\"message\":\"%s\"}",
            log_timestamp, log_level, escaped_message);
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
    (void)request; /* Suppress unused parameter warning */
    struct http_response response;
    memset(&response, 0, sizeof(response));
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    char json[1024];
    const char* active_plugin = plugins_get_active_name();
    
    snprintf(json, sizeof(json),
        "{"
        "\"plugins\":["
        "{"
        "\"name\":\"Classic Phone\","
        "\"description\":\"Traditional pay phone functionality with VoIP calling\","
        "\"active\":%s"
        "},"
        "{"
        "\"name\":\"Fortune Teller\","
        "\"description\":\"Mystical fortune telling experience - 25¢ per fortune\","
        "\"active\":%s"
        "},"
        "{"
        "\"name\":\"Jukebox\","
        "\"description\":\"Coin-operated music player - 25¢ per song\","
        "\"active\":%s"
        "}"
        "],"
        "\"active_plugin\":\"%s\""
        "}",
        (active_plugin && strcmp(active_plugin, "Classic Phone") == 0) ? "true" : "false",
        (active_plugin && strcmp(active_plugin, "Fortune Teller") == 0) ? "true" : "false",
        (active_plugin && strcmp(active_plugin, "Jukebox") == 0) ? "true" : "false",
        active_plugin ? active_plugin : "Unknown"
    );
    
    web_server_strcpy_safe(response.body, json, sizeof(response.body));
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
    if (!server || !client_ip || !endpoint) return 1;
    
    time_t now = time(NULL);
    char key[128];
    snprintf(key, sizeof(key), "%s:%s", client_ip, endpoint);
    
    /* Clean up old entries (older than 60 seconds) */
    int i;
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
    int found_idx = -1;
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
            strncpy(server->rate_limit_keys[found_idx], key, sizeof(server->rate_limit_keys[found_idx]) - 1);
            server->rate_limit_keys[found_idx][sizeof(server->rate_limit_keys[found_idx]) - 1] = '\0';
            server->rate_limit_infos[found_idx].request_count = 0;
            server->rate_limit_count++;
        } else {
            return 1; /* Rate limit table full, allow request */
        }
    }
    
    /* Update counters */
    server->rate_limit_infos[found_idx].request_count++;
    server->rate_limit_infos[found_idx].last_request = now;
    
    return 1; /* Allowed - simplified rate limiting for C89 */
}

struct http_response web_server_create_rate_limit_response(void) {
    struct http_response response;
    memset(&response, 0, sizeof(response));
    response.status_code = 429; /* Too Many Requests */
    
    web_server_strcpy_safe(response.header_keys[response.header_count], "Retry-After", sizeof(response.header_keys[response.header_count]));
    web_server_strcpy_safe(response.header_values[response.header_count], "10", sizeof(response.header_values[response.header_count]));
    response.header_count++;
    
    web_server_strcpy_safe(response.content_type, "application/json", sizeof(response.content_type));
    
    const char* json = 
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

void web_server_broadcast_to_websockets(struct web_server* server, const char* message) {
    if (!server || !message) return;
    
    int i;
    for (i = 0; i < server->websocket_count; i++) {
        int fd = server->websocket_connections[i];
        if (send(fd, message, strlen(message), 0) < 0) {
            /* Connection closed, remove it */
            close(fd);
            /* Shift remaining connections */
            int j;
            for (j = i; j < server->websocket_count - 1; j++) {
                server->websocket_connections[j] = server->websocket_connections[j + 1];
            }
            server->websocket_count--;
            i--; /* Check this index again */
        }
    }
}

/* Streaming response implementation */
int web_server_send_streaming_response(int client_fd, const struct http_response* response) {
    if (!response || !response->is_streaming) return -1;
    
    /* Send HTTP headers first */
    char headers[1024];
    char* ptr = headers;
    size_t remaining = sizeof(headers);
    
    /* Status line */
    int written = snprintf(ptr, remaining, "HTTP/1.1 %d OK\r\n", response->status_code);
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
    int i;
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
    FILE* file = fopen(response->file_path, "rb");
    if (!file) {
        return -1;
    }
    
    char buffer[8192];  /* 8KB buffer for efficient reading */
    size_t bytes_read;
    int total_sent = 0;
    
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
