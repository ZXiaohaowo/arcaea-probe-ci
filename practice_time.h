#ifndef PRACTICE_TIME_H
#define PRACTICE_TIME_H

#include <stdint.h>

/* Pure source-time model. All calls must be serialized by the adapter.
 * Uses monotonic microseconds, never wall-clock epochs or game memory.
 * No audio calls, anchor writes or automatic recovery are performed here.
 * Initialize once per adapter lifetime; do not reinitialize while tickets exist.
 * The adapter must bind on lifecycle events even if allocator addresses repeat. */
typedef enum {
    PT_OK, PT_INVALID, PT_INACTIVE, PT_STALE, PT_NEEDS_PAUSE,
    PT_CLOCK_BACKWARD, PT_CLOCK_GAP, PT_OVERFLOW, PT_FAULTED
} PtResult;

typedef struct { uint64_t generation, revision; } PtTicket;

typedef struct {
    uint64_t generation, revision, last_us, max_gap_us;
    int64_t source_us;
    unsigned rate_percent, remainder; /* remainder is 1/100 microsecond */
    int active, paused, faulted;
} PracticeTime;

/* max_gap_us: 1..1,000,000,000. A longer running gap latches a fault,
 * preserving the last accepted position; never catch up across a suspension.
 * Long gaps while explicitly paused are allowed. */
PtResult pt_init(PracticeTime *c, uint64_t max_gap_us);
/* A new binding always starts paused at 1x and invalidates every old ticket. */
PtResult pt_bind(PracticeTime *c, uint64_t now_us, int64_t source_us);
PtResult pt_invalidate(PracticeTime *c);
PtTicket pt_ticket(const PracticeTime *c);
/* Returns a value only on success. Duplicated timestamp consumes no time. */
PtResult pt_sample(PracticeTime *c, uint64_t now_us, int64_t *source_us);
/* All control operations validate both session generation and state revision.
 * Set rate and rebase require pause. Rebase uses a position measured by the
 * adapter AFTER the game's seek/resume settling; it is not a game seek itself. */
PtResult pt_set_rate(PracticeTime *c, PtTicket ticket, uint64_t now_us, unsigned percent);
PtResult pt_pause(PracticeTime *c, PtTicket ticket, uint64_t now_us);
PtResult pt_resume(PracticeTime *c, PtTicket ticket, uint64_t now_us);
PtResult pt_rebase(PracticeTime *c, PtTicket ticket, uint64_t now_us, int64_t source_us);
/* Numerical projection only, NOT permission to write a game field.
 * For verified mode A: consumer_ms = t20 - adjustment_ms.
 * Floors negative microseconds as well; rejects a t20 outside int32 range.
 * The adapter must establish mode, state, freshness and ordering separately. */
PtResult pt_project_mode_a(int64_t source_us, int32_t adjustment_ms, int32_t *t20_ms);

#endif
