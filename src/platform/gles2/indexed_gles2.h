#ifndef CC_INDEXED_GLES2_H
#define CC_INDEXED_GLES2_H

#include "host.h"
#include "console_common/platform/platform.h"

/* Borrow the existing native host; do not mirror CcPlatform's private layout. */
CcGles2Host *cc_gles2_platform_host(CcPlatform *platform);
void cc_gles2_platform_invalidate_graphics(CcPlatform *platform);

#endif
