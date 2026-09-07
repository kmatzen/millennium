#include "experience_owner.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[256];

static void test_valid_controls_are_canonicalized(void) {
    char path[320];
    char value[256];
    FILE* stream;
    snprintf(path, sizeof(path), "%s/request.json", directory);
    TEST_ASSERT_EQ_INT(experience_owner_submit(directory,
        "{\"ignored\":1,\"id\":\"last-line\",\"action\":\"disable\"}"), 0);
    stream = fopen(path, "rb");
    TEST_ASSERT(stream != NULL);
    memset(value, 0, sizeof(value));
    fread(value, 1, sizeof(value) - 1, stream);
    fclose(stream);
    unlink(path);
    TEST_ASSERT(strcmp(value, "{\"action\":\"disable\",\"id\":\"last-line\"}\n") == 0);
}

static void test_invalid_actions_and_ids_are_rejected(void) {
    TEST_ASSERT_EQ_INT(experience_owner_submit(directory, "{\"action\":\"install\",\"id\":\"x\"}"), -1);
    TEST_ASSERT_EQ_INT(experience_owner_submit(directory, "{\"action\":\"disable\",\"id\":\"../../etc\"}"), -1);
    TEST_ASSERT_EQ_INT(experience_owner_submit(directory, "{\"action\":\"fallback\"}"), 0);
    {
        char path[320];
        snprintf(path, sizeof(path), "%s/request.json", directory);
        unlink(path);
    }
    TEST_ASSERT_EQ_INT(experience_owner_submit(directory, "{\"action\":\"select\",\"id\":\"last-line\"}"), 0);
    {
        char path[320];
        snprintf(path, sizeof(path), "%s/request.json", directory);
        unlink(path);
    }
}

int main(void) {
    snprintf(directory, sizeof(directory), "/tmp/millennium-owner-%ld", (long)getpid());
    if (mkdir(directory, 0700) != 0) return 2;
    TEST_SUITE_BEGIN("Experience owner request boundary");
    TEST_SUITE_RUN(test_valid_controls_are_canonicalized);
    TEST_SUITE_RUN(test_invalid_actions_and_ids_are_rejected);
    rmdir(directory);
    TEST_REPORT();
}
