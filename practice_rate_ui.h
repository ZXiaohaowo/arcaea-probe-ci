#ifndef PRACTICE_RATE_UI_H
#define PRACTICE_RATE_UI_H
#include <stdint.h>
typedef struct {
    int visible,ready,pending,result;
    unsigned applied;
    uint64_t epoch;
} PracticeRateUIState;
/* Main-thread only. The C adapter revalidates every request before writing. */
PracticeRateUIState pcp_rate_ui_state(void);
int pcp_rate_ui_request(unsigned percent,uint64_t epoch);
void practice_rate_ui_start(void);
#endif
