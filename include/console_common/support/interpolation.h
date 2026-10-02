#ifndef CC_SUPPORT_INTERPOLATION_H
#define CC_SUPPORT_INTERPOLATION_H

/* Duration must be positive. Tangents express value change per time unit.
 * Time is not clamped, so callers may deliberately extrapolate past either end. */
static inline float cc_cubic_hermite(float time, float duration, float first,
                                     float first_tangent, float last,
                                     float last_tangent) {
    float fraction = time / duration;
    float square = fraction * fraction;
    float cube = square * fraction;
    return (2 * cube - 3 * square + 1) * first +
           (cube - 2 * square + fraction) * duration * first_tangent +
           (-2 * cube + 3 * square) * last + (cube - square) * duration * last_tangent;
}

#endif
