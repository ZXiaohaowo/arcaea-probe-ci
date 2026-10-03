#ifndef PRACTICE_SEEK_H
#define PRACTICE_SEEK_H
#include <stdint.h>
enum { PSK_IDLE, PSK_QUEUED, PSK_NEW, PSK_READY, PSK_VERIFY, PSK_DONE, PSK_FAILED };
enum { PSK_NONE, PSK_RESTART, PSK_LOCATE, PSK_SUCCESS, PSK_ERROR };
typedef struct {
    int phase, automatic, error, stable, verified;
    uint32_t target, previous_pos, percent;
    int32_t offset;
    uint64_t id, generation, origin, deadline, located_us;
} PracticeSeek;
typedef struct {
    uint64_t now_us, generation;
    int valid, playing, can_restart;
    uint32_t pos, percent;
    int64_t consumer;
} PracticeSeekSample;
int psk_busy(const PracticeSeek *s);
int psk_request(PracticeSeek *s,uint32_t target,int automatic,uint64_t gen,uint64_t now);
/* An explicit native retry replaces the old operation and supplies its restart. */
void psk_cancel(PracticeSeek *s);
int psk_follow_retry(PracticeSeek *s,uint32_t target,uint64_t gen,uint64_t now);
int psk_poll(PracticeSeek *s,PracticeSeekSample x);
void psk_fail(PracticeSeek *s,int error);
#endif
