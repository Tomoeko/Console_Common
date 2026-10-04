#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "console_common/render/texture_cache.h"

#include "console_common/render/image.h"

#include "image_internal.h"
#include "texture_source.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CC_MAX_TEXTURE_SOURCES = 8192 };

typedef enum CcTextureState {
    CC_TEXTURE_UNLOADED,
    CC_TEXTURE_RESIDENT,
    CC_TEXTURE_FAILED
} CcTextureState;

typedef struct CcTextureEntry {
    char *url;
    uint32_t handle;
    size_t bytes;
    uint64_t frame_used;
    uint64_t recent_use;
    CcTextureState state;
} CcTextureEntry;

struct CcTextureCache {
    CcPlatform *platform;
    intptr_t raw_root_directory;
    CcTextureEntry *entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t budget_bytes;
    size_t resident_bytes;
    uint64_t frame;
    uint64_t use_clock;
    uint64_t evictions;
};

static CcTextureEntry *find_entry(CcTextureCache *cache, const char *url,
                                  size_t *insertion_index) {
    size_t low = 0;
    size_t high = cache->entry_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int comparison = strcmp(cache->entries[middle].url, url);
        if (comparison < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *insertion_index = low;
    if (low < cache->entry_count && strcmp(cache->entries[low].url, url) == 0) {
        return &cache->entries[low];
    }
    return NULL;
}

static CcTextureEntry *insert_entry(CcTextureCache *cache, const char *url,
                                    size_t url_length, size_t index) {
    if (cache->entry_count >= CC_MAX_TEXTURE_SOURCES) {
        return NULL;
    }
    char *copy = malloc(url_length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, url, url_length + 1);

    if (cache->entry_count == cache->entry_capacity) {
        size_t capacity = cache->entry_capacity ? cache->entry_capacity * 2 : 64;
        if (capacity > CC_MAX_TEXTURE_SOURCES) {
            capacity = CC_MAX_TEXTURE_SOURCES;
        }
        CcTextureEntry *entries = realloc(cache->entries, capacity * sizeof(*entries));
        if (!entries) {
            free(copy);
            return NULL;
        }
        cache->entries = entries;
        cache->entry_capacity = capacity;
    }

    memmove(cache->entries + index + 1, cache->entries + index,
            (cache->entry_count - index) * sizeof(*cache->entries));
    CcTextureEntry *entry = cache->entries + index;
    *entry = (CcTextureEntry){.url = copy};
    cache->entry_count++;
    return entry;
}

static CcTextureEntry *least_recent_eviction_candidate(CcTextureCache *cache) {
    CcTextureEntry *candidate = NULL;
    for (size_t index = 0; index < cache->entry_count; ++index) {
        CcTextureEntry *entry = cache->entries + index;
        if (entry->state != CC_TEXTURE_RESIDENT || entry->frame_used == cache->frame) {
            continue;
        }
        if (!candidate || entry->recent_use < candidate->recent_use) {
            candidate = entry;
        }
    }
    return candidate;
}

static bool reserve_gpu_bytes(CcTextureCache *cache, size_t bytes) {
    if (bytes > cache->budget_bytes) {
        return false;
    }
    while (cache->resident_bytes > cache->budget_bytes - bytes) {
        CcTextureEntry *victim = least_recent_eviction_candidate(cache);
        if (!victim) {
            return false;
        }
        cc_platform_destroy_texture(cache->platform, victim->handle);
        cache->resident_bytes -= victim->bytes;
        victim->handle = 0;
        victim->state = CC_TEXTURE_UNLOADED;
        cache->evictions++;
    }
    return true;
}

CcTextureCache *cc_texture_cache_create(CcPlatform *platform, const char *raw_root,
                                        size_t budget_bytes) {
    if (!platform || !raw_root || !raw_root[0] || budget_bytes == 0) {
        return NULL;
    }
    intptr_t root_directory = cc_texture_source_root_open(raw_root);
    if (root_directory < 0) {
        return NULL;
    }
    CcTextureCache *cache = calloc(1, sizeof(*cache));
    if (!cache) {
        cc_texture_source_root_close(root_directory);
        return NULL;
    }
    cache->platform = platform;
    cache->raw_root_directory = root_directory;
    cache->budget_bytes = budget_bytes;
    cache->frame = 1;
    return cache;
}

void cc_texture_cache_destroy(CcTextureCache *cache) {
    if (!cache) {
        return;
    }
    for (size_t index = 0; index < cache->entry_count; ++index) {
        CcTextureEntry *entry = cache->entries + index;
        if (entry->state == CC_TEXTURE_RESIDENT) {
            cc_platform_destroy_texture(cache->platform, entry->handle);
        }
        free(entry->url);
    }
    free(cache->entries);
    cc_texture_source_root_close(cache->raw_root_directory);
    free(cache);
}

void cc_texture_cache_begin_frame(CcTextureCache *cache) {
    if (!cache) {
        return;
    }
    if (cache->frame == UINT64_MAX) {
        for (size_t index = 0; index < cache->entry_count; ++index) {
            cache->entries[index].frame_used = 0;
        }
        cache->frame = 1;
    } else {
        cache->frame++;
    }
}

static void record_use(CcTextureCache *cache, CcTextureEntry *entry) {
    if (cache->use_clock == UINT64_MAX) {
        for (size_t index = 0; index < cache->entry_count; ++index) {
            cache->entries[index].recent_use = 0;
        }
        cache->use_clock = 0;
    }
    entry->frame_used = cache->frame;
    entry->recent_use = ++cache->use_clock;
}

bool cc_texture_cache_resolve(CcTextureCache *cache, const char *relative_png_url,
                              uint32_t *handle) {
    if (handle) {
        *handle = 0;
    }
    if (!cache || !handle) {
        return false;
    }
    size_t url_length;
    if (!cc_texture_source_url_valid(relative_png_url, &url_length)) {
        return false;
    }

    size_t insertion_index;
    CcTextureEntry *entry = find_entry(cache, relative_png_url, &insertion_index);
    if (!entry) {
        entry = insert_entry(cache, relative_png_url, url_length, insertion_index);
        if (!entry) {
            return false;
        }
    }
    if (entry->state == CC_TEXTURE_RESIDENT) {
        record_use(cache, entry);
        *handle = entry->handle;
        return true;
    }
    if (entry->state == CC_TEXTURE_FAILED) {
        return false;
    }
    if (entry->bytes != 0 && !reserve_gpu_bytes(cache, entry->bytes)) {
        return false;
    }

    FILE *source =
        cc_texture_source_open(cache->raw_root_directory, relative_png_url, url_length);
    if (!source) {
        entry->state = CC_TEXTURE_FAILED;
        return false;
    }
    size_t declared_bytes;
    if (!cc_image_declared_bytes(source, &declared_bytes)) {
        fclose(source);
        entry->state = CC_TEXTURE_FAILED;
        return false;
    }
    entry->bytes = declared_bytes;
    if (declared_bytes > cache->budget_bytes) {
        fclose(source);
        entry->state = CC_TEXTURE_FAILED;
        return false;
    }
    if (!reserve_gpu_bytes(cache, declared_bytes)) {
        fclose(source);
        return false;
    }
    CcImage image;
    bool read_successful = cc_image_read_bounded_stream(source, declared_bytes, &image);
    fclose(source);
    if (!read_successful) {
        entry->state = CC_TEXTURE_FAILED;
        return false;
    }

    size_t bytes = (size_t)image.width * image.height * 4;
    entry->bytes = bytes;
    if (bytes != declared_bytes || bytes > cache->budget_bytes) {
        entry->state = CC_TEXTURE_FAILED;
        cc_image_free(&image);
        return false;
    }
    if (!reserve_gpu_bytes(cache, bytes)) {
        cc_image_free(&image);
        return false;
    }

    uint32_t texture = cc_platform_create_texture(cache->platform, (int)image.width,
                                                  (int)image.height, image.pixels);
    cc_image_free(&image);
    if (texture == 0) {
        entry->state = CC_TEXTURE_FAILED;
        return false;
    }
    entry->state = CC_TEXTURE_RESIDENT;
    entry->handle = texture;
    cache->resident_bytes += bytes;
    record_use(cache, entry);
    *handle = texture;
    return true;
}

void cc_texture_cache_retry_failed(CcTextureCache *cache) {
    if (!cache) {
        return;
    }
    for (size_t index = 0; index < cache->entry_count; ++index) {
        CcTextureEntry *entry = cache->entries + index;
        if (entry->state == CC_TEXTURE_FAILED) {
            entry->state = CC_TEXTURE_UNLOADED;
            entry->bytes = 0;
        }
    }
}

CcTextureCacheStats cc_texture_cache_stats(const CcTextureCache *cache) {
    CcTextureCacheStats statistics = {0};
    if (!cache) {
        return statistics;
    }
    statistics.budget_bytes = cache->budget_bytes;
    statistics.resident_bytes = cache->resident_bytes;
    statistics.known_sources = cache->entry_count;
    statistics.evictions = cache->evictions;
    for (size_t index = 0; index < cache->entry_count; ++index) {
        CcTextureState state = cache->entries[index].state;
        if (state == CC_TEXTURE_RESIDENT) {
            statistics.resident_textures++;
        } else if (state == CC_TEXTURE_FAILED) {
            statistics.failed_sources++;
        }
    }
    return statistics;
}
