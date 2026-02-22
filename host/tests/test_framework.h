#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

static int tf_pass_count = 0;
static int tf_fail_count = 0;

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        tf_fail_count++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQ_INT(a, b) do { \
    int _a = (a); int _b = (b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == %d, expected %d\n", \
                __FILE__, __LINE__, #a, _a, _b); \
        tf_fail_count++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQ_STR(a, b) do { \
    const char *_a = (a); const char *_b = (b); \
    if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == \"%s\", expected \"%s\"\n", \
                __FILE__, __LINE__, #a, _a ? _a : "(null)", _b ? _b : "(null)"); \
        tf_fail_count++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_NULL(a) do { \
    if ((a) != NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s should be NULL\n", \
                __FILE__, __LINE__, #a); \
        tf_fail_count++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_NOT_NULL(a) do { \
    if ((a) == NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s should not be NULL\n", \
                __FILE__, __LINE__, #a); \
        tf_fail_count++; \
        return; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    fprintf(stderr, "  %-50s", #fn); \
    fn(); \
    if (tf_fail_count == _prev_fail) { \
        tf_pass_count++; \
        fprintf(stderr, "PASS\n"); \
    } \
} while(0)

#define TEST_SUITE_BEGIN(name) do { \
    fprintf(stderr, "\n== %s ==\n", name); \
} while(0)

#define TEST_SUITE_RUN(fn) do { \
    int _prev_fail = tf_fail_count; \
    RUN_TEST(fn); \
} while(0)

#define TEST_REPORT() do { \
    fprintf(stderr, "\n---------------------------\n"); \
    fprintf(stderr, "  %d passed, %d failed\n", tf_pass_count, tf_fail_count); \
    fprintf(stderr, "---------------------------\n"); \
    return tf_fail_count > 0 ? 1 : 0; \
} while(0)

#endif /* TEST_FRAMEWORK_H */
