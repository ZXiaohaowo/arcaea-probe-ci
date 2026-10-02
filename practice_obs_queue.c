#include "practice_obs_queue.h"

int poq_init(PracticeObsQueue *q)
{
    atomic_flag_clear_explicit(&q->gate, memory_order_relaxed);
    atomic_init(&q->attempted, 0u);
    atomic_init(&q->accepted, 0u);
    atomic_init(&q->dropped_busy, 0u);
    atomic_init(&q->dropped_full, 0u);
    atomic_init(&q->drained, 0u);
    q->head = q->count = 0;
    return atomic_is_lock_free(&q->attempted) &&
           atomic_is_lock_free(&q->accepted) &&
           atomic_is_lock_free(&q->dropped_busy) &&
           atomic_is_lock_free(&q->dropped_full) &&
           atomic_is_lock_free(&q->drained);
}

PoqResult poq_push(PracticeObsQueue *q, const PoqRecord *record)
{
    atomic_fetch_add_explicit(&q->attempted, 1u, memory_order_relaxed);
    if (atomic_flag_test_and_set_explicit(&q->gate, memory_order_acquire)) {
        atomic_fetch_add_explicit(&q->dropped_busy, 1u, memory_order_relaxed);
        return POQ_BUSY;
    }
    if (q->count == POQ_CAPACITY) {
        atomic_fetch_add_explicit(&q->dropped_full, 1u, memory_order_relaxed);
        atomic_flag_clear_explicit(&q->gate, memory_order_release);
        return POQ_FULL;
    }
    q->records[(q->head + q->count) % POQ_CAPACITY] = *record;
    ++q->count;
    atomic_fetch_add_explicit(&q->accepted, 1u, memory_order_relaxed);
    atomic_flag_clear_explicit(&q->gate, memory_order_release);
    return POQ_OK;
}

PoqResult poq_pop(PracticeObsQueue *q, PoqRecord *record)
{
    if (atomic_flag_test_and_set_explicit(&q->gate, memory_order_acquire))
        return POQ_BUSY;
    if (!q->count) {
        atomic_flag_clear_explicit(&q->gate, memory_order_release);
        return POQ_EMPTY;
    }
    *record = q->records[q->head];
    q->head = (q->head + 1u) % POQ_CAPACITY;
    --q->count;
    atomic_fetch_add_explicit(&q->drained, 1u, memory_order_relaxed);
    atomic_flag_clear_explicit(&q->gate, memory_order_release);
    return POQ_OK;
}

PoqStats poq_stats(const PracticeObsQueue *q)
{
    PoqStats s;
    s.attempted = atomic_load_explicit(&q->attempted, memory_order_relaxed);
    s.accepted = atomic_load_explicit(&q->accepted, memory_order_relaxed);
    s.dropped_busy = atomic_load_explicit(&q->dropped_busy, memory_order_relaxed);
    s.dropped_full = atomic_load_explicit(&q->dropped_full, memory_order_relaxed);
    s.drained = atomic_load_explicit(&q->drained, memory_order_relaxed);
    return s;
}
