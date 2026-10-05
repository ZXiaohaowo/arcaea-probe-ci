#import <UIKit/UIKit.h>
#import <CoreText/CoreText.h>
#include "practice_rate_ui.h"
#include <math.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "practice_note_plan.h"
static _Atomic uint64_t note_settings_word = (uint64_t)100 << 32;
static void publish_note_settings(void);
#include "practice_timeline_ui.inc"

void practice_rate_ui_transition_begin(void) {
    [[NSNotificationCenter defaultCenter] postNotificationName:@"PCPTransitionBegin" object:nil];
}
unsigned pcp_rate_ui_stored_percent(void)
{
    NSInteger saved=[[NSUserDefaults standardUserDefaults] integerForKey:@"PCPPracticeRatePercent"];
    if(saved>=PCP_RATE_MIN_PERCENT && saved<=250) {
        unsigned bounded=(unsigned)MIN(saved,(NSInteger)PCP_RATE_MAX_PERCENT);
        if(bounded!=(unsigned)saved) pcp_rate_ui_save_percent(bounded);
        return bounded;
    }
    return 100u;
}

void pcp_rate_ui_save_percent(unsigned percent) {
    if(percent>=PCP_RATE_MIN_PERCENT && percent<=PCP_RATE_MAX_PERCENT) {
        [[NSUserDefaults standardUserDefaults] setInteger:percent forKey:@"PCPPracticeRatePercent"];
        publish_note_settings();
    }
}

unsigned pcp_note_mode_get(void) {
    NSInteger saved=[[NSUserDefaults standardUserDefaults] integerForKey:@"PCPNoteSpeedMode"];
    return saved==1?PCP_NOTE_MODE_FIXED:PCP_NOTE_MODE_SYNC;
}

unsigned pcp_pitch_mode_get(void) {
    NSInteger saved=[[NSUserDefaults standardUserDefaults] integerForKey:@"PCPPitchMode"];
    return saved==1?PCP_PITCH_KEEP:PCP_PITCH_TAPE;
}

unsigned pcp_pitch_comp_get(void) {
    NSInteger v=[[NSUserDefaults standardUserDefaults] integerForKey:@"PCPPitchCompMs"];
    if(v<0) v=0;
    if(v>80) v=80;
    return (unsigned)v;
}

void pcp_pitch_comp_set(unsigned ms) {
    if(ms>80) ms=80;
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)ms forKey:@"PCPPitchCompMs"];
}

void pcp_pitch_mode_set(unsigned mode) {
    [[NSUserDefaults standardUserDefaults] setInteger:(mode==PCP_PITCH_KEEP?1:0)
                                               forKey:@"PCPPitchMode"];
}

void pcp_note_mode_set(unsigned mode) {
    [[NSUserDefaults standardUserDefaults] setInteger:(mode==PCP_NOTE_MODE_FIXED?1:0)
                                               forKey:@"PCPNoteSpeedMode"];
    publish_note_settings();
}
unsigned pcp_note_reference_get(void) {
    NSInteger value=[NSUserDefaults.standardUserDefaults integerForKey:@"PCPNoteReferenceMilli"];
    return value>=500 && value<=30000?(unsigned)value:0;
}
static void publish_note_settings(void) {
    uint64_t word=pnp_pack(pcp_note_reference_get(),pcp_rate_ui_stored_percent(),pcp_note_mode_get());
    atomic_store_explicit(&note_settings_word,word,memory_order_release);
}
uint64_t pcp_note_settings_snapshot(void) {
    return atomic_load_explicit(&note_settings_word,memory_order_acquire);
}
void pcp_note_current_set(double current) {
    if(pcp_note_mode_get()!=PCP_NOTE_MODE_SYNC)return;
    unsigned reference=pnp_reference(current,100,0);
    if(!reference)return;
    [NSUserDefaults.standardUserDefaults setInteger:reference forKey:@"PCPNoteReferenceMilli"];
    publish_note_settings();
}
void pcp_note_native_reset(void) {
    if(pcp_note_mode_get()!=PCP_NOTE_MODE_SYNC)return;
    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"PCPNoteReferenceMilli"];
    publish_note_settings();
}

void practice_rate_ui_start(void) {
    publish_note_settings();
    dispatch_async(dispatch_get_main_queue(), ^{
        static PCPRatePanel *controller;
        if(controller) return;
        PTRegisterFonts();
        controller=[PCPRatePanel new];[controller build];
        controller.timer=[NSTimer timerWithTimeInterval:0.2 target:controller selector:@selector(tick) userInfo:nil repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:controller.timer forMode:NSRunLoopCommonModes];
        controller.countdownTimer=[NSTimer timerWithTimeInterval:1.0/30 target:controller selector:@selector(countdownTick) userInfo:nil repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:controller.countdownTimer forMode:NSRunLoopCommonModes];
    });
}
