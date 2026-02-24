#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct web_server;
struct http_request;
struct http_response;

/* Daemon state structure */
struct daemon_state_info {
    int current_state;
    int inserted_cents;
    char keypad_buffer[64];
    time_t last_activity;
    int sip_registered;       /* 0=unknown, 1=ok, -1=failed */
    char sip_last_error[128]; /* Last registration error if failed */
};

/* HTTP Request structure */
struct http_request {
    char method[16];
    char path[256];
    char body[4096];
    char client_ip[64];
    /* Headers and query params stored as key-value pairs */
    char header_keys[32][64];
    char header_values[32][256];
    int header_count;
    char query_keys[16][64];
    char query_values[16][256];
    int query_count;
};

/* HTTP Response structure */
struct http_response {
    int status_code;
    char body[8192];  /* For small responses */
    char content_type[64];
    /* Headers stored as key-value pairs */
    char header_keys[16][64];
    char header_values[16][256];
    int header_count;
    
    /* Streaming response support */
    int is_streaming;
    char file_path[256];  /* Path to file for streaming */
    size_t content_length;  /* Total content length for streaming */
};

/* Route handler function type */
typedef struct http_response (*route_handler_t)(const struct http_request* req);

/* WebSocket handler function type */
typedef void (*websocket_handler_t)(int client_fd);

/* Rate limit info structure */
struct rate_limit_info {
    time_t last_request;
    int request_count;
};

/* WebServer structure */
struct web_server {
    int port;
    int running;
    int should_stop;
    int paused;
    int server_fd;
    pthread_t server_thread;
    
    /* Route storage - using arrays instead of maps */
    char route_methods[32][16];
    char route_paths[32][256];
    route_handler_t route_handlers[32];
    int route_count;
    
    /* Static routes */
    char static_paths[16][256];
    char static_file_paths[16][256];  /* File paths for large files */
    char static_contents[16][8192];   /* Small content for inline responses */
    char static_content_types[16][64];
    int static_is_file[16];  /* 1 if using file path, 0 if using content */
    int static_count;
    
    /* WebSocket connections */
    int websocket_connections[32];
    int websocket_count;
    websocket_handler_t websocket_handler;
    
    /* Rate limiting */
    char rate_limit_keys[64][128];
    struct rate_limit_info rate_limit_infos[64];
    int rate_limit_count;
};

/* WebServer API functions */
struct web_server* web_server_create(int port);
void web_server_destroy(struct web_server* server);
void web_server_start(struct web_server* server);
void web_server_stop(struct web_server* server);
/* Pause/resume: when paused, API requests return 503. Use during audio to avoid
 * web traffic disrupting playback. Behavior during calls: same — all requests
 * rejected until resume (#111). */
void web_server_pause(struct web_server* server);
void web_server_resume(struct web_server* server);
int web_server_is_running(const struct web_server* server);
int web_server_is_paused(const struct web_server* server);

/* Configuration */
void web_server_set_port(struct web_server* server, int port);
int web_server_get_port(const struct web_server* server);

/* Route management */
void web_server_add_route(struct web_server* server, const char* method, const char* path, route_handler_t handler);
void web_server_add_static_route(struct web_server* server, const char* path, const char* content, const char* content_type);
void web_server_add_file_route(struct web_server* server, const char* path, const char* file_path, const char* content_type);

/* API endpoints setup */
void web_server_setup_api_routes(struct web_server* server);

/* WebSocket support */
void web_server_add_websocket_route(struct web_server* server, const char* path, websocket_handler_t handler);
void web_server_broadcast_to_websockets(struct web_server* server, const char* message);

/* HTTP parsing and response functions */
struct http_request web_server_parse_request(const char* raw_request);
struct http_response web_server_process_request(struct web_server* server, const struct http_request* request);
char* web_server_serialize_response(const struct http_response* response);
int web_server_send_streaming_response(int client_fd, const struct http_response* response);

/* Internal server functions */
void web_server_init_socket(struct web_server* server);
void web_server_handle_client(struct web_server* server, int client_fd);

/* Helper functions */
char* web_server_url_decode(const char* str, char* result, size_t result_size);
void web_server_parse_query_string(const char* query, struct http_request* request);
int web_server_is_websocket_upgrade(const struct http_request* request);

/* Default handlers */
struct http_response web_server_handle_not_found(const struct http_request* request);
struct http_response web_server_handle_api_status(const struct http_request* request);
struct http_response web_server_handle_api_metrics(const struct http_request* request);
struct http_response web_server_handle_api_health(const struct http_request* request);
struct http_response web_server_handle_api_config(const struct http_request* request);
struct http_response web_server_handle_api_state(const struct http_request* request);
struct http_response web_server_handle_api_control(const struct http_request* request);
struct http_response web_server_handle_api_logs(const struct http_request* request);
struct http_response web_server_handle_api_plugins(const struct http_request* request);
struct http_response web_server_handle_api_update(const struct http_request* request);
struct http_response web_server_handle_api_version(const struct http_request* request);
struct http_response web_server_handle_api_check_update(const struct http_request* request);
struct http_response web_server_handle_dashboard(const struct http_request* request);

/* Utility functions */
int web_server_is_in_call(void);
int web_server_is_ringing(void);
int web_server_is_high_priority_state(void);
int web_server_is_audio_active(void);
int web_server_check_rate_limit(struct web_server* server, const char* client_ip, const char* endpoint);
struct http_response web_server_create_rate_limit_response(void);

/* String utility functions */
void web_server_strcpy_safe(char* dest, const char* src, size_t dest_size);
void web_server_strcat_safe(char* dest, const char* src, size_t dest_size);
int web_server_strcmp_safe(const char* str1, const char* str2);
char* web_server_strstr_safe(const char* haystack, const char* needle);
size_t web_server_strlen_safe(const char* str);

/* Memory management */
void* web_server_malloc(size_t size);
void web_server_free(void* ptr);

/* Daemon integration functions */
time_t get_daemon_start_time(void);
struct daemon_state_info get_daemon_state_info(void);
int send_control_command(const char* action);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */