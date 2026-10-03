#ifndef PRACTICE_POINTS_H
#define PRACTICE_POINTS_H
#include <stdint.h>
#include <stddef.h>
/* IDs remain stable even when another bookmark is deleted: A,B,T1,T2,T3. */
#define PP_COUNT 5
typedef struct {
    uint32_t ms[PP_COUNT], draft, duration;
    unsigned mask, revision;
    int selected, editing;
} PracticePoints;
void pp_init(PracticePoints *p,uint32_t duration);
int pp_set(PracticePoints *p,unsigned id,uint32_t ms);
int pp_select(PracticePoints *p,unsigned id); /* 0 reject, 1 select, 2 edit */
int pp_adjust(PracticePoints *p,int delta); /* +/-100 or +/-1000 ms */
int pp_input(PracticePoints *p,uint32_t ms);
int pp_confirm(PracticePoints *p);
void pp_cancel(PracticePoints *p);
int pp_delete_t(PracticePoints *p,unsigned id);
int pp_format(uint32_t ms,char *label,size_t n,char *detail,size_t m);
#endif
