#ifndef SIP_TEXT_H
#define SIP_TEXT_H
#include <stddef.h>
void sip_text_copy(char *destination, size_t destination_size,
                   const char *source, long source_length);
#endif
