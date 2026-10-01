#ifndef CONSOLE_COMMON_SUPPORT_SHA1_H
#define CONSOLE_COMMON_SUPPORT_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct CcSha1 {
    uint32_t words[5];
    uint64_t byte_count;
    uint8_t pending[64];
    size_t pending_count;
} CcSha1;

void cc_sha1_init(CcSha1 *sha1);
void cc_sha1_update(CcSha1 *sha1, const uint8_t *data, size_t length);
void cc_sha1_final(CcSha1 *sha1, uint8_t digest[20]);

#endif
