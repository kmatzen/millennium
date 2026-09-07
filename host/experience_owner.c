#define _POSIX_C_SOURCE 200809L
#include "experience_owner.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

static int extract_field(const char* body, const char* field, char* output,
                         size_t output_size) {
    char marker[64];
    const char* start;
    const char* end;
    size_t length;
    if (!body || !field || !output || output_size == 0) return -1;
    if (snprintf(marker, sizeof(marker), "\"%s\":\"", field) < 0) return -1;
    start = strstr(body, marker);
    if (!start) return -1;
    start += strlen(marker);
    end = strchr(start, '"');
    if (!end) return -1;
    length = (size_t)(end - start);
    if (length == 0 || length >= output_size) return -1;
    memcpy(output, start, length);
    output[length] = '\0';
    return 0;
}

static int valid_id(const char* value) {
    const unsigned char* cursor = (const unsigned char*)value;
    size_t length = value ? strlen(value) : 0;
    if (length < 1 || length > 64 || (!islower(cursor[0]) && !isdigit(cursor[0]))) return 0;
    for (; *cursor; cursor++) {
        if (!islower(*cursor) && !isdigit(*cursor) && *cursor != '.' &&
            *cursor != '_' && *cursor != '-') return 0;
    }
    return 1;
}

int experience_owner_read_status(const char* path, char* output, size_t output_size) {
    FILE* stream;
    size_t count;
    int extra;
    if (!path || !output || output_size < 3) return -1;
    stream = fopen(path, "rb");
    if (!stream) {
        strcpy(output, "{}");
        return errno == ENOENT ? 0 : -1;
    }
    count = fread(output, 1, output_size - 1, stream);
    extra = fgetc(stream);
    fclose(stream);
    output[count] = '\0';
    if (extra != EOF || count < 2 || output[0] != '{') return -1;
    return 0;
}

int experience_owner_submit(const char* directory, const char* body) {
    char action[16];
    char id[65];
    char temporary[512];
    char final[512];
    char encoded[160];
    int descriptor;
    ssize_t length;
    int needs_id;
    if (!directory || extract_field(body, "action", action, sizeof(action)) != 0) return -1;
    needs_id = strcmp(action, "enable") == 0 || strcmp(action, "disable") == 0 ||
               strcmp(action, "select") == 0;
    if (!needs_id && strcmp(action, "fallback") != 0) return -1;
    if (needs_id && (extract_field(body, "id", id, sizeof(id)) != 0 || !valid_id(id))) return -1;
    if (snprintf(temporary, sizeof(temporary), "%s/.request.%ld", directory,
                 (long)getpid()) >= (int)sizeof(temporary) ||
        snprintf(final, sizeof(final), "%s/request.json", directory) >= (int)sizeof(final)) return -1;
    if (needs_id)
        length = snprintf(encoded, sizeof(encoded), "{\"action\":\"%s\",\"id\":\"%s\"}\n", action, id);
    else
        length = snprintf(encoded, sizeof(encoded), "{\"action\":\"fallback\"}\n");
    if (length < 0 || (size_t)length >= sizeof(encoded)) return -1;
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0640);
    if (descriptor < 0) return -1;
    if (write(descriptor, encoded, (size_t)length) != length || fsync(descriptor) != 0) {
        close(descriptor);
        unlink(temporary);
        return -1;
    }
    if (close(descriptor) != 0) {
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, final) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}
