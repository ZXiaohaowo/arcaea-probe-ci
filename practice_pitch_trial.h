#ifndef PRACTICE_PITCH_TRIAL_H
#define PRACTICE_PITCH_TRIAL_H
typedef struct {
    void *context;
    int (*get)(void *, float *);
    int (*set)(void *, float);
} PracticePitchOps;
typedef struct {
    float baseline, target, observed, restored;
    int baseline_rc, set_rc, read_rc, restore_rc, final_rc;
    int attempted, target_ok, restore_ok;
} PracticePitchTrial;
/* Caller must prove a live paused channel, serialize on its owner thread,
 * and keep ownership for the whole synchronous transaction.
 * Once any set is attempted, ALWAYS attempt restoration, even if set failed.
 * Return is success only when target and restoration are both confirmed.
 * restore_ok=false after attempted=true requires explicit recovery. */
int ppt_run(PracticePitchOps ops, unsigned percent, PracticePitchTrial *report);
#endif
