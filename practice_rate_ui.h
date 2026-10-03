#ifndef PRACTICE_RATE_UI_H
#define PRACTICE_RATE_UI_H
#include <stdint.h>
#include "practice_points.h"
#define PCP_RATE_MIN_PERCENT 50u
#define PCP_RATE_MAX_PERCENT 200u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int visible,ready,pending,result,prep;
    unsigned applied;
    uint64_t epoch;
    uint64_t open_seq;
} PracticeRateUIState;
/* Main-thread only. The C adapter revalidates every request before writing. */
PracticeRateUIState pcp_rate_ui_state(void);
typedef struct {
    int visible, score_valid, scroll_valid, pitch_attached;
    unsigned percent, pure, far, lost;
    double accuracy, current_note_speed, note_speed;
} PracticeHUDState;
/* Main-thread read-only snapshot. Unknown fields must render as --. */
PracticeHUDState pcp_hud_state(void);
int pcp_rate_ui_request(unsigned percent,uint64_t epoch);
/* Persistent setting (NSUserDefaults); 100 when absent or invalid. */
unsigned pcp_rate_ui_stored_percent(void);
void pcp_rate_ui_save_percent(unsigned percent);
/* Note-speed display mode (NSUserDefaults, key PCPNoteSpeedMode).
 * 0 = notes scale with the practice rate (sync, default), 1 = fixed visual
 * speed (the scroll scalar is divided by the rate by the tag8 bridge). */
#define PCP_NOTE_MODE_SYNC  0u
#define PCP_NOTE_MODE_FIXED 1u
unsigned pcp_note_mode_get(void);
void pcp_note_mode_set(unsigned mode);
/* v29: pitch preservation mode. 0 = tape effect (pitch follows rate, default),
 * 1 = keep pitch (attach the built-in pitch DSP with ratio 1/rate). */
#define PCP_PITCH_TAPE 0u
#define PCP_PITCH_KEEP 1u
unsigned pcp_pitch_mode_get(void);
void pcp_pitch_mode_set(unsigned mode);
/* v30: keep-pitch latency compensation in milliseconds (persisted). The
 * channel position is advanced by rate*comp once when the DSP is attached. */
unsigned pcp_pitch_comp_get(void);
void pcp_pitch_comp_set(unsigned ms);
/* v28: capture the current paused audio position as a bookmark (first step of
 * the A/B work; no seek yet). Returns the total bookmark count. */
unsigned pcp_bookmark_add(void);
/* v32: A/B practice points (source time + chart time captured while paused)
 * and the manual/loop jump request. which: 0 = A, 1 = B. */
unsigned pcp_ab_set(unsigned which);
unsigned pcp_ab_have(unsigned which);
unsigned pcp_ab_get(unsigned which);
unsigned pcp_ab_jumps(void);
void pcp_ab_jump(void);
unsigned pcp_seek_phase(void);
unsigned pcp_seek_error(void);
PracticePoints pcp_points_snapshot(void);
unsigned pcp_points_current(void);
unsigned pcp_points_extent(void);
uint64_t pcp_points_epoch(void);
unsigned pcp_loop_suspended(void);
int pcp_point_capture(unsigned id);
int pcp_point_update(unsigned id,unsigned ms);
int pcp_point_delete(unsigned id);
int pcp_point_jump(unsigned id);
int pcp_native_retry(void);
unsigned pcp_ab_loop_get(void);
void pcp_ab_loop_set(unsigned on);
/* Native entry callback: request that the rate panel is shown. */
void pcp_rate_ui_open(void);
void practice_rate_ui_start(void);
#ifdef __cplusplus
}
#endif
#endif
