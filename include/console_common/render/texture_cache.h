#ifndef CONSOLE_COMMON_TEXTURE_CACHE_H
#define CONSOLE_COMMON_TEXTURE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "console_common/platform/platform.h"

typedef struct CcTextureCache CcTextureCache;

typedef struct CcTextureCacheStats {
    size_t budget_bytes;
    size_t resident_bytes;
    size_t resident_textures;
    size_t failed_sources;
    size_t known_sources;
    uint64_t evictions;
} CcTextureCacheStats;

/* raw_root must be an existing directory containing prepared .wmra images.
 * Destroy the cache after the last platform_end for its submitted quads. */
CcTextureCache *cc_texture_cache_create(CcPlatform *platform, const char *raw_root,
                                        size_t budget_bytes);
void cc_texture_cache_destroy(CcTextureCache *cache);

/* Call once before each frame's draw submissions. A texture requested during
 * this frame cannot be evicted until the next begin_frame call. */
void cc_texture_cache_begin_frame(CcTextureCache *cache);

/* Accepts a relative prepared .png URL and loads its sibling .wmra file.
 * Returns false with handle zero when invalid, absent, or over budget. */
bool cc_texture_cache_resolve(CcTextureCache *cache, const char *relative_png_url,
                              uint32_t *handle);

/* Missing/corrupt files and failed uploads are not retried each frame. Call
 * after preparing assets again to retry those entries on demand. */
void cc_texture_cache_retry_failed(CcTextureCache *cache);
CcTextureCacheStats cc_texture_cache_stats(const CcTextureCache *cache);

#endif
