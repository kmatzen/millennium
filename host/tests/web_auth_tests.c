#include "web_server.h"
#include "test_framework.h"
#include <string.h>

static void add_header(struct http_request* req, const char* key,
                       const char* value) {
    int i = req->header_count++;
    strncpy(req->header_keys[i], key, sizeof(req->header_keys[i]) - 1);
    strncpy(req->header_values[i], value, sizeof(req->header_values[i]) - 1);
}

static void init_admin_request(struct http_request* req) {
    memset(req, 0, sizeof(*req));
    strcpy(req->method, "POST");
    strcpy(req->path, "/api/control");
    add_header(req, "Host", "phone.kmatzen.com");
}

static void test_missing_server_token_denies(void) {
    struct web_server server;
    struct http_request req;
    memset(&server, 0, sizeof(server));
    init_admin_request(&req);
    add_header(&req, "Authorization", "Bearer correct-token");
    TEST_ASSERT_EQ_INT(web_server_request_is_admin_authorized(&server, &req), 0);
}

static void test_missing_or_wrong_bearer_denies(void) {
    struct web_server server;
    struct http_request req;
    memset(&server, 0, sizeof(server));
    web_server_set_admin_token(&server, "correct-token");
    init_admin_request(&req);
    TEST_ASSERT_EQ_INT(web_server_request_is_admin_authorized(&server, &req), 0);
    add_header(&req, "Authorization", "Bearer wrong-token");
    TEST_ASSERT_EQ_INT(web_server_request_is_admin_authorized(&server, &req), 0);
}

static void test_correct_bearer_authorizes(void) {
    struct web_server server;
    struct http_request req;
    memset(&server, 0, sizeof(server));
    web_server_set_admin_token(&server, "correct-token");
    init_admin_request(&req);
    add_header(&req, "Authorization", "Bearer correct-token");
    TEST_ASSERT_EQ_INT(web_server_request_is_admin_authorized(&server, &req), 1);
}

static void test_cross_origin_denies(void) {
    struct web_server server;
    struct http_request req;
    memset(&server, 0, sizeof(server));
    web_server_set_admin_token(&server, "correct-token");
    init_admin_request(&req);
    add_header(&req, "Origin", "https://evil.example");
    add_header(&req, "Authorization", "Bearer correct-token");
    TEST_ASSERT_EQ_INT(web_server_request_is_admin_authorized(&server, &req), 0);
}

static void test_same_or_allowed_origin_authorizes(void) {
    struct web_server server;
    struct http_request req;
    memset(&server, 0, sizeof(server));
    web_server_set_admin_token(&server, "correct-token");
    init_admin_request(&req);
    add_header(&req, "Origin", "https://phone.kmatzen.com");
    add_header(&req, "Authorization", "Bearer correct-token");
    TEST_ASSERT_EQ_INT(web_server_request_is_admin_authorized(&server, &req), 1);

    web_server_set_allowed_origin(&server, "https://maintenance.kmatzen.com");
    strcpy(req.header_values[1], "https://maintenance.kmatzen.com");
    TEST_ASSERT_EQ_INT(web_server_request_is_admin_authorized(&server, &req), 1);
}

static void test_rate_limit_enforces_admin_and_separates_clients(void) {
    struct web_server server;
    int i;
    memset(&server, 0, sizeof(server));
    pthread_mutex_init(&server.state_mutex, NULL);
    for (i = 0; i < 30; i++) {
        TEST_ASSERT_EQ_INT(1, web_server_check_rate_limit(
            &server, "192.0.2.1", "/api/control"));
    }
    TEST_ASSERT_EQ_INT(0, web_server_check_rate_limit(
        &server, "192.0.2.1", "/api/control"));
    TEST_ASSERT_EQ_INT(1, web_server_check_rate_limit(
        &server, "192.0.2.2", "/api/control"));
    TEST_ASSERT_EQ_INT(1, web_server_check_rate_limit(
        &server, "192.0.2.1", "/api/health"));
    pthread_mutex_destroy(&server.state_mutex);
}

int main(void) {
    TEST_SUITE_BEGIN("Web administration authorization");
    TEST_SUITE_RUN(test_missing_server_token_denies);
    TEST_SUITE_RUN(test_missing_or_wrong_bearer_denies);
    TEST_SUITE_RUN(test_correct_bearer_authorizes);
    TEST_SUITE_RUN(test_cross_origin_denies);
    TEST_SUITE_RUN(test_same_or_allowed_origin_authorizes);
    TEST_SUITE_RUN(test_rate_limit_enforces_admin_and_separates_clients);
    TEST_REPORT();
}
