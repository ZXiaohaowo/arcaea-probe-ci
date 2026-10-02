#include "practice_time.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static PtResult ready(const PracticeTime *c)
{
    if (!c || !c->max_gap_us) return PT_INVALID;
    if (!c->active) return PT_INACTIVE;
    if (c->faulted) return PT_FAULTED;
    return PT_OK;
}

static PtResult check(const PracticeTime *c, PtTicket t)
{
    PtResult r = ready(c);
    if (r != PT_OK) return r;
    if (t.generation != c->generation || t.revision != c->revision)
        return PT_STALE;
    if (c->revision == UINT64_MAX) return PT_OVERFLOW;
    return PT_OK;
}

static PtResult fault(PracticeTime *c, PtResult why)
{
    c->faulted = 1;
    if (c->revision != UINT64_MAX) c->revision++;
    return why;
}

static PtResult advance(PracticeTime *c, uint64_t now)
{
    uint64_t dt, numerator, amount;
    PtResult r = ready(c);
    if (r != PT_OK) return r;
    if (now < c->last_us) return fault(c, PT_CLOCK_BACKWARD);
    dt = now - c->last_us;
    if (!c->paused) {
        if (dt > c->max_gap_us) return fault(c, PT_CLOCK_GAP);
        /* dt <= 1e9, rate <= 250, so these products cannot overflow. */
        numerator = dt * c->rate_percent + c->remainder;
        amount = numerator / 100;
        if (c->source_us > INT64_MAX - (int64_t)amount)
            return fault(c, PT_OVERFLOW);
        c->source_us += (int64_t)amount;
        c->remainder = (unsigned)(numerator % 100);
    }
    c->last_us = now;
    return PT_OK;
}

PtResult pt_init(PracticeTime *c, uint64_t max_gap_us)
{
    if (!c || max_gap_us == 0 || max_gap_us > 1000000000ULL) return PT_INVALID;
    memset(c, 0, sizeof(*c));
    c->max_gap_us = max_gap_us;
    c->rate_percent = 100;
    c->paused = 1;
    return PT_OK;
}

PtResult pt_bind(PracticeTime *c, uint64_t now, int64_t source)
{
    if (!c || !c->max_gap_us) return PT_INVALID;
    if (c->generation == UINT64_MAX || c->revision == UINT64_MAX) return PT_OVERFLOW;
    c->generation++;
    c->revision++;
    c->last_us = now;
    c->source_us = source;
    c->rate_percent = 100;
    c->remainder = 0;
    c->active = 1;
    c->paused = 1;
    c->faulted = 0;
    return PT_OK;
}

PtResult pt_invalidate(PracticeTime *c)
{
    if (!c || !c->max_gap_us) return PT_INVALID;
    if (c->generation == UINT64_MAX || c->revision == UINT64_MAX) {
        c->active = 0;
        return PT_OVERFLOW;
    }
    c->generation++;
    c->revision++;
    c->active = 0;
    c->paused = 1;
    return PT_OK;
}

PtTicket pt_ticket(const PracticeTime *c)
{
    PtTicket t = {0, 0};
    if (c) { t.generation = c->generation; t.revision = c->revision; }
    return t;
}

PtResult pt_sample(PracticeTime *c, uint64_t now, int64_t *source)
{
    PtResult r;
    if (!source) return PT_INVALID;
    r = advance(c, now);
    if (r == PT_OK) *source = c->source_us;
    return r;
}

PtResult pt_set_rate(PracticeTime *c, PtTicket t, uint64_t now, unsigned percent)
{
    PtResult r = check(c, t);
    if (r != PT_OK) return r;
    if (percent < 50 || percent > 250) return PT_INVALID;
    if (!c->paused) return PT_NEEDS_PAUSE;
    r = advance(c, now);
    if (r != PT_OK) return r;
    c->rate_percent = percent;
    c->revision++;
    return PT_OK;
}

PtResult pt_pause(PracticeTime *c, PtTicket t, uint64_t now)
{
    PtResult r = check(c, t);
    if (r != PT_OK) return r;
    r = advance(c, now);
    if (r != PT_OK) return r;
    if (!c->paused) { c->paused = 1; c->revision++; }
    return PT_OK;
}

PtResult pt_resume(PracticeTime *c, PtTicket t, uint64_t now)
{
    PtResult r = check(c, t);
    if (r != PT_OK) return r;
    r = advance(c, now);
    if (r != PT_OK) return r;
    if (c->paused) { c->paused = 0; c->revision++; }
    return PT_OK;
}

PtResult pt_rebase(PracticeTime *c, PtTicket t, uint64_t now, int64_t source)
{
    PtResult r = check(c, t);
    if (r != PT_OK) return r;
    if (!c->paused) return PT_NEEDS_PAUSE;
    r = advance(c, now);
    if (r != PT_OK) return r;
    c->source_us = source;
    c->remainder = 0;
    c->revision++;
    return PT_OK;
}

PtResult pt_project_mode_a(int64_t source, int32_t adjustment, int32_t *t20)
{
    int64_t ms = source / 1000;
    if (!t20) return PT_INVALID;
    if (source % 1000 < 0) ms--;
    /* source / 1000 leaves ample headroom for the int32 adjustment. */
    ms += adjustment;
    if (ms < INT32_MIN || ms > INT32_MAX) return PT_OVERFLOW;
    *t20 = (int32_t)ms;
    return PT_OK;
}
