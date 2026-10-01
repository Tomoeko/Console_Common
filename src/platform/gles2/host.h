#ifndef CC_GLES2_HOST_H
#define CC_GLES2_HOST_H

#include "console_common/platform/platform.h"

/* X11 and EGL stay behind this private interface. The host owns the native
 * window and current ES2 context; the renderer owns every GL object. */
typedef struct CcGles2Host CcGles2Host;

CcGles2Host *cc_gles2_host_create(const char *title, int width, int height);
void cc_gles2_host_show(CcGles2Host *host);
bool cc_gles2_host_make_current(CcGles2Host *host);
/* Optional EGL capability; failure leaves the full redraw path available. */
bool cc_gles2_host_preserve_back_buffer(CcGles2Host *host);
void cc_gles2_host_destroy(CcGles2Host *host);

bool cc_gles2_host_poll(CcGles2Host *host, CcEvent *event);
void cc_gles2_host_surface_size(CcGles2Host *host, int *width, int *height);
bool cc_gles2_host_present(CcGles2Host *host);

#endif
