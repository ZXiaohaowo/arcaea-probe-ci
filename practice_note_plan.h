#ifndef PRACTICE_NOTE_PLAN_H
#define PRACTICE_NOTE_PLAN_H
#include <stdint.h>
#include <math.h>
/* One atomic word: reference speed in thousandths (0 = native), rate, mode.
 * A custom CurrentNoteSpeed entered at r in fixed mode stores current*r.
 * A later rate change can then preserve physical speed at the next build. */
static inline uint64_t pnp_pack(unsigned reference, unsigned rate, unsigned mode) {
    return (uint64_t)reference | ((uint64_t)rate<<32) | ((uint64_t)mode<<40);
}
static inline int pnp_plan(uint64_t word, float native, float raw, float *scale, float *out) {
    unsigned ref=(unsigned)word,rate=(unsigned)(word>>32)&255u,mode=(unsigned)(word>>40);
    if(!scale || !out || rate<50 || rate>200 || mode>1 || ref>60000 ||
       !isfinite(native) || native<0.1f || native>30 || !isfinite(raw) || raw<=0 || raw>=100) return 0;
    double base=ref?ref/1000.0:native;
    double current=base*(mode?100.0/rate:1.0), factor=current/native, value=raw*factor;
    if(!isfinite(value) || current<=0 || current>120 || factor<=0 || factor>1200 || value>120000) return 0;
    *scale=(float)factor;*out=(float)value;return 1;
}
static inline unsigned pnp_reference(double current, unsigned rate, unsigned mode) {
    if(!isfinite(current) || current<0.5 || current>30 || rate<50 || rate>200 || mode>1)return 0;
    return (unsigned)llround(current*1000.0*(mode?rate/100.0:1.0));
}
#endif
