#ifndef PRACTICE_RATE_BIAS_H
#define PRACTICE_RATE_BIAS_H
#include <stdint.h>
/* Serialized W1 delta model. Keeps native t28/t34 outside its ownership.
 * Binding needs a real lifecycle event; pointers alone are not generations.
 * At 1x the accumulated offset is retained for continuous restoration.
 * This module is NOT wired into the v23 client: lifecycle/audio coordination
 * must be established before its returned value can replace native w9. */
typedef struct {
    uint64_t last_us;
    int64_t bias_us;
    int32_t last_native;
    int fraction, paused, faulted, bound;
    unsigned percent;
} PracticeRateBias;
void prb_bind(PracticeRateBias *s, uint64_t now, int32_t native);
int prb_apply(PracticeRateBias *s, unsigned percent);
int prb_step(PracticeRateBias *s, uint64_t now, int32_t native, int paused, int32_t *out);
#endif
