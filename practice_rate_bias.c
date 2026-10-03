#include "practice_rate_bias.h"
#include <limits.h>
#include <string.h>

void prb_bind(PracticeRateBias *s, uint64_t now, int32_t native)
{
    memset(s, 0, sizeof(*s));
    s->last_us=now; s->last_native=native;
    s->percent=100; s->paused=1; s->bound=1;
}

int prb_apply(PracticeRateBias *s, unsigned percent)
{
    if (!s || !s->bound || s->faulted || !s->paused || percent<50 || percent>250) return 0;
    s->percent=percent;
    return 1;
}

int prb_step(PracticeRateBias *s, uint64_t now, int32_t native, int paused, int32_t *out)
{
    int64_t numerator, increment, ms, delta;
    uint64_t dt;
    if (!s || !out || !s->bound || s->faulted) return 0;
    if (now<s->last_us) goto fault;
    dt=now-s->last_us;
    if (!s->paused && dt>250000) goto fault;
    /* Detect an unannounced native-time reset/jump in a running interval. */
    if (!s->paused && !paused) {
        delta=((int64_t)native-s->last_native)*1000-(int64_t)dt;
        if (delta>50000 || delta< -50000) goto fault;
    }
    if (!s->paused) {
        numerator=(int64_t)dt*((int)s->percent-100)+s->fraction;
        increment=numerator/100;
        if ((increment>0 && s->bias_us>INT64_MAX-increment) ||
            (increment<0 && s->bias_us<INT64_MIN-increment)) goto fault;
        s->bias_us+=increment;
        s->fraction=(int)(numerator%100);
    }
    ms=s->bias_us/1000;
    if (s->bias_us%1000<0 || (s->bias_us%1000==0 && s->fraction<0)) --ms;
    ms+=(int64_t)native;
    if (ms<INT32_MIN || ms>INT32_MAX) goto fault;
    s->last_us=now; s->last_native=native; s->paused=!!paused;
    *out=(int32_t)ms;
    return 1;
fault:
    s->faulted=1;
    return 0;
}
