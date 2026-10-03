#include "practice_rate_session.h"
#include <math.h>
#include <string.h>

static int equal(float a,float b) {return isfinite(a) && fabsf(a-b)<0.0001f;}
/* Soft restore (v26): teardown must never latch. Drops ownership on a dead or
 * failing handle; the caller keeps the diagnostic result codes. handle_gone is
 * set when the chain no longer owns the handle (the normal exit case). */
static int restore_soft(PracticeRateSession *s,int *handle_gone)
{
    if (!s->audio_owned) return 1;
    s->restores++;
    s->restore_rc=s->restore_read_rc=-998;
    if (!s->is_current(s->audio.context)) {
        s->audio_owned=0;
        if (handle_gone) *handle_gone=1;
        return 0;
    }
    s->restore_rc=s->audio.set(s->audio.context,s->baseline);
    s->restore_read_rc=s->audio.get(s->audio.context,&s->restored);
    if (s->restore_rc || s->restore_read_rc || !equal(s->restored,s->baseline)) {
        s->audio_owned=0;
        if (handle_gone) *handle_gone=0;
        return 0;
    }
    s->audio_owned=0;
    return 1;
}

int prs_begin(PracticeRateSession *s,uint64_t now,int32_t native,unsigned pct,
              PracticePitchOps audio,int (*current)(void *))
{
    if (!s || s->ever || !audio.get || !audio.set || !current || pct<50 || pct>250) return 0;
    s->ever=1;s->audio=audio;s->is_current=current;s->percent=pct;
    s->apply_rc=s->read_rc=s->restore_rc=s->restore_read_rc=-999;
    if (!current(audio.context) || audio.get(audio.context,&s->baseline) || !equal(s->baseline,1.0f)) {
        s->errors++;return 0;
    }
    s->applied=(float)pct/100.0f;
    s->audio_owned=1; /* setter failure may still have changed state */
    s->apply_rc=audio.set(audio.context,s->applied);
    s->read_rc=audio.get(audio.context,&s->readback);
    if (s->apply_rc || s->read_rc || !equal(s->readback,s->applied)) {
        s->errors++;restore_soft(s,NULL);return 0;
    }
    prb_bind(&s->clock,now,native);
    if (!prb_apply(&s->clock,pct)) {s->errors++;restore_soft(s,NULL);return 0;}
    s->phase=1;
    return 1;
}

int prs_live_begin(PracticeRateSession *s,uint64_t now,int32_t native,unsigned pct)
{
    if (!s || !s->phase || pct<50 || pct>250) return 0;
    if (!s->is_current(s->audio.context)) {prs_close(s,6);return 0;}
    /* Rebase at the play boundary (the native start correction already happened),
     * keep the accumulated offset and re-assert the rate in case the game reset it. */
    s->clock.last_us=now;s->clock.last_native=native;
    s->clock.paused=1;s->clock.faulted=0;
    s->percent=pct;s->applied=(float)pct/100.0f;s->audio_owned=1;
    s->apply_rc=s->audio.set(s->audio.context,s->applied);
    s->read_rc=s->audio.get(s->audio.context,&s->readback);
    if (s->apply_rc || s->read_rc || !equal(s->applied,s->readback)) {
        s->errors++;prs_fallback(s,now,native,1,8);return 0;
    }
    if (!prb_apply(&s->clock,pct)) {s->errors++;prs_fallback(s,now,native,1,8);return 0;}
    s->phase=pct==100?2:1;
    s->manual_control=1;
    if (pct==100) {s->audio_owned=0;s->restored=s->readback;}
    return 1;
}

void prs_fallback(PracticeRateSession *s,uint64_t now,int32_t native,int paused,int reason)
{
    int gone=0;
    if (!s || !s->phase) return;
    if (!restore_soft(s,&gone) && !gone) s->errors++;
    /* Explicit recovery retains the already-earned offset. No fresh bias=0 bind. */
    s->clock.percent=100;s->clock.last_us=now;s->clock.last_native=native;
    s->clock.paused=!!paused;s->clock.faulted=0;
    if (gone) {s->phase=0;s->clock.bound=0;}
    else {s->phase=2;}
    s->close_reason=reason;
}

int32_t prs_tick(PracticeRateSession *s,uint64_t now,int32_t native,int paused)
{
    int32_t out=native;
    if (!s || !s->phase) return native;
    s->ticks++;
    if (!prb_step(&s->clock,now,native,paused,&out)) {
        s->errors++;prs_fallback(s,now,native,paused,4);
        if (!prb_step(&s->clock,now,native,paused,&out)) {prs_close(s,5);return native;}
    }
    if (!paused) s->seen_play=1;
    else if (s->phase==1 && s->seen_play && !s->manual_control) {
        prs_fallback(s,now,native,1,1); /* next pause ends this rate trial */
    }
    return out;
}

int prs_configure(PracticeRateSession *s,uint64_t now,int32_t native,unsigned pct,
                  PracticePitchOps audio,int (*current)(void *))
{
    if (!s || pct<50 || pct>250) return 0;
    if (!s->phase) {
        if (s->audio_owned) return 0;
        memset(s,0,sizeof(*s));
        if (!prs_begin(s,now,native,pct,audio,current)) return 0;
        s->manual_control=1;
        return 1;
    }
    if (!s->clock.paused || s->clock.faulted || !s->is_current(s->audio.context)) return 0;
    /* Apply transaction while paused; accumulated chart offset is unchanged. */
    s->audio_owned=1;
    s->applied=(float)pct/100.0f;
    s->apply_rc=s->audio.set(s->audio.context,s->applied);
    s->read_rc=s->audio.get(s->audio.context,&s->readback);
    if (s->apply_rc || s->read_rc || !equal(s->applied,s->readback)) {
        s->errors++;prs_fallback(s,now,native,1,8);return 0;
    }
    if (!prb_apply(&s->clock,pct)) {
        s->errors++;prs_fallback(s,now,native,1,8);return 0;
    }
    s->percent=pct;s->phase=pct==100?2:1;s->manual_control=1;
    if (pct==100) {s->audio_owned=0;s->restored=s->readback;}
    return 1;
}

void prs_close(PracticeRateSession *s,int reason)
{
    if (!s) return;
    (void)restore_soft(s,NULL);
    s->phase=0;s->clock.bound=0;s->close_reason=reason;
    /* Soft close: evidence stays, ownership does not, no permanent latch. */
}
