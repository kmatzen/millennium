#ifndef EXPERIENCE_OWNER_H
#define EXPERIENCE_OWNER_H

#include <stddef.h>

int experience_owner_read_status(const char* path, char* output, size_t output_size);
int experience_owner_submit(const char* directory, const char* body);

#endif
