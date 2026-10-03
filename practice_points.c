#include "practice_points.h"
#include <stdio.h>
#include <string.h>
static uint32_t limit(const PracticePoints *p) {return p->duration?p->duration:INT32_MAX;}
void pp_init(PracticePoints *p,uint32_t duration) {
    memset(p,0,sizeof(*p));p->selected=-1;
    p->duration=duration>INT32_MAX?INT32_MAX:duration;
}
int pp_set(PracticePoints *p,unsigned id,uint32_t ms) {
    if(id>=PP_COUNT || ms>limit(p)) return 0;
    p->ms[id]=ms;p->mask|=1u<<id;p->revision++;return 1;
}
int pp_select(PracticePoints *p,unsigned id) {
    if(id>=PP_COUNT || !(p->mask&(1u<<id))) return 0;
    if(p->editing) return 0; /* finish or cancel the existing draft first */
    if(p->selected==(int)id) {p->editing=1;p->draft=p->ms[id];return 2;}
    p->selected=(int)id;return 1;
}
int pp_adjust(PracticePoints *p,int delta) {
    if(!p->editing || (delta!=100 && delta!=-100 && delta!=1000 && delta!=-1000)) return 0;
    int64_t value=(int64_t)p->draft+delta;
    if(value<0) value=0;
    if(value>limit(p)) value=limit(p);
    p->draft=(uint32_t)value;return 1;
}
int pp_input(PracticePoints *p,uint32_t ms) {
    if(!p->editing || ms>limit(p)) return 0;
    p->draft=ms;return 1;
}
int pp_confirm(PracticePoints *p) {
    if(!p->editing || p->selected<0) return 0;
    int result=pp_set(p,(unsigned)p->selected,p->draft);
    if(result) p->editing=0;
    return result;
}
void pp_cancel(PracticePoints *p) {p->editing=0;}
int pp_delete_t(PracticePoints *p,unsigned id) {
    if(id<2 || id>=PP_COUNT || p->editing || !(p->mask&(1u<<id))) return 0;
    p->mask&=~(1u<<id);p->revision++;
    if(p->selected==(int)id) p->selected=-1;
    return 1;
}
int pp_format(uint32_t ms,char *label,size_t n,char *detail,size_t m) {
    if(!label || !detail || !n || !m) return 0;
    int a=snprintf(label,n,"%02u:%02u.%03u",ms/60000,(ms/1000)%60,ms%1000);
    int b=snprintf(detail,m,"%u ms",ms);
    return a>=0 && (size_t)a<n && b>=0 && (size_t)b<m;
}
