#include "practice_pitch_trial.h"
#include <math.h>
#include <string.h>

static int same(float a,float b) { return isfinite(a) && fabsf(a-b)<=0.0001f; }
int ppt_run(PracticePitchOps o,unsigned percent,PracticePitchTrial *r)
{
    if (!r) return 0;
    memset(r,0,sizeof(*r));
    r->baseline_rc=r->set_rc=r->read_rc=r->restore_rc=r->final_rc=-999;
    if (!o.get || !o.set || percent<50 || percent>250) return 0;
    r->baseline_rc=o.get(o.context,&r->baseline);
    if (r->baseline_rc || !isfinite(r->baseline) || r->baseline<=0 || r->baseline>4) return 0;
    r->target=r->baseline*(float)percent/100.0f;
    r->attempted=1;
    r->set_rc=o.set(o.context,r->target);
    r->read_rc=o.get(o.context,&r->observed);
    r->target_ok=!r->set_rc && !r->read_rc && same(r->observed,r->target);
    r->restore_rc=o.set(o.context,r->baseline);
    r->final_rc=o.get(o.context,&r->restored);
    r->restore_ok=!r->restore_rc && !r->final_rc && same(r->restored,r->baseline);
    return r->target_ok && r->restore_ok;
}
