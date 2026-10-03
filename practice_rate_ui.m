#import <UIKit/UIKit.h>
#include "practice_rate_ui.h"
#include <math.h>
#include <stdlib.h>
#include "practice_timeline_ui.inc"

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
    if(percent>=PCP_RATE_MIN_PERCENT && percent<=PCP_RATE_MAX_PERCENT)
        [[NSUserDefaults standardUserDefaults] setInteger:percent forKey:@"PCPPracticeRatePercent"];
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
}

void practice_rate_ui_start(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        static PCPRatePanel *controller;
        if(controller) return;
        controller=[PCPRatePanel new];[controller build];
        controller.timer=[NSTimer timerWithTimeInterval:0.2 target:controller selector:@selector(tick) userInfo:nil repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:controller.timer forMode:NSRunLoopCommonModes];
    });
}
