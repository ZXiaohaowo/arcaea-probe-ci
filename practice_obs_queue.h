#ifndef PRACTICE_OBS_QUEUE_H
#define PRACTICE_OBS_QUEUE_H

#include <stdatomic.h>
#include <stdint.h>

/* Observation only. Contention/full means a counted drop, never a spin or wait.
 * Initialize before exposing callbacks; never reset while callbacks can run.
 * Payload and cursors are accessed ONLY while gate is held. No overwrite.
 * Counters wrap modulo UINT_MAX; snapshots during activity are approximate.
 * Queue order is admission order, NOT proof of execution order across threads.
 */
#define POQ_CAPACITY 8192u
typedef struct {
    uint64_t mach, tag, object, lr, thread;
    uint64_t invocation; /* adapter-supplied pair ID; zero means unavailable */
    int32_t t20, t28;
    uint32_t state, valid; /* adapter defines bits; invalid reads stay invalid */
} PoqRecord;

typedef struct {
    atomic_flag gate;
    atomic_uint attempted, accepted, dropped_busy, dropped_full, drained;
    unsigned head, count;
    PoqRecord records[POQ_CAPACITY];
} PracticeObsQueue;

/* Required for static or automatic storage before poq_init. */
#define POQ_INITIALIZER { .gate = ATOMIC_FLAG_INIT }

typedef enum { POQ_OK, POQ_BUSY, POQ_FULL, POQ_EMPTY } PoqResult;
typedef struct {
    unsigned attempted, accepted, dropped_busy, dropped_full, drained;
} PoqStats;

/* False: do not wire observation callbacks on this platform. */
int poq_init(PracticeObsQueue *q);
PoqResult poq_push(PracticeObsQueue *q, const PoqRecord *record);
PoqResult poq_pop(PracticeObsQueue *q, PoqRecord *record);
PoqStats poq_stats(const PracticeObsQueue *q);

#endif
