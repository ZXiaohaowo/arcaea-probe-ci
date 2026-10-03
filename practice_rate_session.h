#ifndef PRACTICE_RATE_SESSION_H
#define PRACTICE_RATE_SESSION_H
#include "practice_rate_bias.h"
#include "practice_pitch_trial.h"
/* Serialized by the native main thread; UI config supports repeated pauses. The adapter must
 * deliver teardown/replacement BEFORE ownership ends and call begin only while
 * natively paused. No pointer, thread or mode admission is inferred here. */
typedef struct {
    PracticeRateBias clock;
    PracticePitchOps audio;
    int (*is_current)(void *);
    unsigned percent;
    int ever, phase, audio_owned, seen_play, manual_control;
    int apply_rc, read_rc, restore_rc, restore_read_rc, errors, close_reason;
    float baseline, applied, readback, restored;
    uint64_t ticks, restores;
} PracticeRateSession;
/* phase: 0 inactive, 1 configured rate, 2 1x with continuity offset retained.
 * v26: every close is best-effort; a dead handle drops ownership without a
 * permanent error latch. The adapter re-arms a fresh session per song. */
int prs_begin(PracticeRateSession *, uint64_t, int32_t, unsigned,
              PracticePitchOps, int (*is_current)(void *));
/* Called at the first live (playing) sample of a song: rebases the clock at the
 * boundary, keeps the accumulated offset, and re-asserts/verifies the audio rate. */
int prs_live_begin(PracticeRateSession *, uint64_t, int32_t, unsigned);
int32_t prs_tick(PracticeRateSession *, uint64_t, int32_t, int paused);
void prs_close(PracticeRateSession *, int reason);
void prs_fallback(PracticeRateSession *, uint64_t, int32_t, int paused, int reason);
/* UI adapter must freshly prove pause, identity and ownership before each call. */
int prs_configure(PracticeRateSession *, uint64_t, int32_t, unsigned,
                  PracticePitchOps, int (*is_current)(void *));
#endif
