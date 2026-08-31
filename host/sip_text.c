#include "sip_text.h"
#include <string.h>

void sip_text_copy(char *destination, size_t destination_size,
                   const char *source, long source_length) {
    size_t length;
    if (!destination || destination_size == 0) return;
    destination[0] = '\0';
    if (!source || source_length <= 0) return;
    length = (size_t)source_length;
    if (length >= destination_size) length = destination_size - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}
