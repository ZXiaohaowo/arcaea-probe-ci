#ifndef PRACTICE_RATE_UI_H
#define PRACTICE_RATE_UI_H
#include <stdint.h>
typedef struct {
    int visible,ready,pending,result,prep;
    unsigned applied;
    uint64_t epoch;
    uint64_t open_seq;
} PracticeRateUIState;
/* Main-thread only. The C adapter revalidates every request before writing. */
PracticeRateUIState pcp_rate_ui_state(void);
int pcp_rate_ui_request(unsigned percent,uint64_t epoch);
/* Persistent setting (NSUserDefaults); 100 when absent or invalid. */
unsigned pcp_rate_ui_stored_percent(void);
/* Native entry callback: request that the rate panel is shown. */
void pcp_rate_ui_open(void);
void practice_rate_ui_start(void);
#endif
