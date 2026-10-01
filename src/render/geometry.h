#ifndef CONSOLE_COMMON_RENDER_GEOMETRY_H
#define CONSOLE_COMMON_RENDER_GEOMETRY_H

#include "console_common/platform/platform.h"

enum { CC_QUAD_CORNERS = 4, CC_VERTICES_PER_QUAD = 6 };

/* Both backends expand corners in this order to preserve the same diagonal
 * and winding for basic and material quads. */
static const unsigned cc_quad_triangle_order[CC_VERTICES_PER_QUAD] = {0, 1, 3, 0, 3, 2};

/* Build left-top, right-top, left-bottom, right-bottom corners. The caller
 * decides whether the quad's dimensions are drawable for its backend. */
static inline void cc_render_quad_corners(const CcQuad *quad,
                                          CcDrawVertex corners[CC_QUAD_CORNERS]) {
    corners[0] = (CcDrawVertex){quad->x, quad->y, quad->u0, quad->v0, quad->color};
    corners[1] =
        (CcDrawVertex){quad->x + quad->width, quad->y, quad->u1, quad->v0, quad->color};
    corners[2] = (CcDrawVertex){quad->x, quad->y + quad->height, quad->u0, quad->v1,
                                quad->color};
    corners[3] = (CcDrawVertex){quad->x + quad->width, quad->y + quad->height, quad->u1,
                                quad->v1, quad->color};
}

#endif
