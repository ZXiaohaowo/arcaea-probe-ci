#include "practice_seek.h"
#include <stdlib.h>
int psk_busy(const PracticeSeek *s) { return s->phase>=PSK_QUEUED && s->phase<=PSK_VERIFY; }
void psk_fail(PracticeSeek *s,int error) { s->phase=PSK_FAILED;s->error=error; }
int psk_request(PracticeSeek *s,uint32_t target,int automatic,uint64_t gen,uint64_t now) {
    if(psk_busy(s) || !now || target>INT32_MAX) return 0;
    uint64_t id=s->id+1;
    *s=(PracticeSeek){.phase=PSK_QUEUED,.automatic=automatic,.target=target,
        .id=id,.origin=gen,.generation=gen,.deadline=now+30000000};
    return 1;
}
int psk_poll(PracticeSeek *s,PracticeSeekSample x) {
    if(!psk_busy(s)) return PSK_NONE;
    if(x.now_us>=s->deadline) {psk_fail(s,1);return PSK_ERROR;}
    if(s->phase==PSK_QUEUED) {
        if(x.generation!=s->origin) {psk_fail(s,2);return PSK_ERROR;}
        if(x.valid && x.can_restart) {s->phase=PSK_NEW;return PSK_RESTART;}
        return PSK_NONE;
    }
    if(s->phase==PSK_NEW) {
        if(x.valid && x.generation!=s->origin) {
            s->generation=x.generation;s->phase=PSK_READY;s->stable=0;
        } else return PSK_NONE;
    }
    if(x.generation!=s->generation) {psk_fail(s,2);return PSK_ERROR;}
    if(!x.valid || !x.playing || x.pos==0 || x.consumer<=0 || x.percent<50 || x.percent>200) {
        s->stable=0;s->verified=0;return PSK_NONE;
    }
    int64_t offset=x.consumer-(int64_t)x.pos;
    if(s->phase==PSK_READY) {
        if(llabs(offset)>20000) {s->stable=0;return PSK_NONE;}
        if(!s->stable || x.pos<=s->previous_pos || llabs(offset-s->offset)>75) s->stable=1;
        else s->stable++;
        s->previous_pos=x.pos;s->offset=(int32_t)offset;
        if(s->stable>=3) {
            s->percent=x.percent;s->phase=PSK_VERIFY;s->located_us=x.now_us;
            s->deadline=x.now_us+8000000;return PSK_LOCATE;
        }
    } else if(s->phase==PSK_VERIFY) {
        if(x.now_us<s->located_us || x.percent!=s->percent) {psk_fail(s,3);return PSK_ERROR;}
        int64_t expected=(int64_t)s->target+(int64_t)((x.now_us-s->located_us)*s->percent/100000);
        if(llabs((int64_t)x.pos-expected)>250 || llabs(offset-s->offset)>150) {
            psk_fail(s,4);return PSK_ERROR;
        }
        if(++s->verified>=2) {s->phase=PSK_DONE;return PSK_SUCCESS;}
    }
    return PSK_NONE;
}
