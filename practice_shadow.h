#ifndef PRACTICE_SHADOW_H
#define PRACTICE_SHADOW_H
#include "practice_time.h"
#include "practice_obs_queue.h"

/* Serialized diagnostic consumer of captured tag2 records. No game pointers are
 * dereferenced. Segments are diagnostic windows, NOT lifecycle generations.
 * Pause/mode/object/gap boundaries explicitly end a window; no continuity claim
 * is made across them. Errors never silently rebase an active window. */
typedef struct {
    PracticeTime time;
    uint64_t object, segments, compared, skipped, faults, transitions;
    uint64_t over5ms, over50ms, max_abs_us, last_us;
    int64_t last_error_us;
    int bound, paused;
} PracticeShadow;
int ps_init(PracticeShadow *s);
int ps_mach_us(uint64_t ticks, uint32_t numer, uint32_t denom, uint64_t *us);
void ps_observe(PracticeShadow *s, const PoqRecord *r, uint32_t numer, uint32_t denom);
#endif
