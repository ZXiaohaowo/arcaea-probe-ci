#include "practice_shadow.h"
#include <limits.h>
#include <string.h>

int ps_init(PracticeShadow *s)
{
    if (!s) return 0;
    memset(s, 0, sizeof(*s));
    return pt_init(&s->time, 250000) == PT_OK;
}

int ps_mach_us(uint64_t ticks, uint32_t n, uint32_t d, uint64_t *us)
{
    uint64_t divisor, whole, rest, fraction;
    if (!n || !d || !us) return 0;
    divisor = (uint64_t)d * 1000u;
    whole = ticks / divisor;
    rest = ticks % divisor;
    if (whole > UINT64_MAX / n || rest > UINT64_MAX / n) return 0;
    fraction = rest * n / divisor;
    if (whole * n > UINT64_MAX - fraction) return 0;
    *us = whole * n + fraction;
    return 1;
}

void ps_observe(PracticeShadow *s, const PoqRecord *r, uint32_t n, uint32_t d)
{
    uint64_t now, absolute;
    int pause;
    int64_t observed, predicted;
    if (r->tag != 2) return;
    if ((r->valid & 5u) != 5u || !r->object ||
        ((r->state >> 8) & 255u) != 1u ||
        !ps_mach_us(r->mach, n, d, &now)) {
        s->skipped++;
        s->bound = 0;
        return;
    }
    pause = (r->state & 255u) != 0;
    observed = ((int64_t)r->t20 - (int64_t)r->t28) * 1000;
    if (s->bound && (now < s->last_us || now - s->last_us > 250000)) {
        s->faults++;
        s->bound = 0;
    }
    if (!s->bound || s->object != r->object || s->paused != pause) {
        if (s->bound) s->transitions++;
        if (pt_bind(&s->time, now, observed) != PT_OK ||
            (!pause && pt_resume(&s->time, pt_ticket(&s->time), now) != PT_OK)) {
            s->faults++;
            s->bound = 0;
            return;
        }
        s->bound = 1;
        s->object = r->object;
        s->paused = pause;
        s->last_us = now;
        s->segments++;
        return;
    }
    if (pt_sample(&s->time, now, &predicted) != PT_OK) {
        s->faults++;
        s->bound = 0;
        return;
    }
    s->last_us = now;
    s->last_error_us = predicted - observed;
    absolute = s->last_error_us < 0 ? (uint64_t)(-s->last_error_us) : (uint64_t)s->last_error_us;
    if (absolute > s->max_abs_us) s->max_abs_us = absolute;
    s->over5ms += absolute > 5000;
    s->over50ms += absolute > 50000;
    s->compared++;
}
