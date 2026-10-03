#import <UIKit/UIKit.h>
#include "practice_rate_ui.h"
#include <math.h>

/* A subview of the existing game window; outside controls touches pass through. */
@interface PCPRateOverlay : UIView
@end
@implementation PCPRateOverlay
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)event {
    UIView *hit=[super hitTest:p withEvent:event];
    return hit==self?nil:hit;
}
@end

@interface PCPRatePanel : NSObject
@property(nonatomic,strong) PCPRateOverlay *overlay;
@property(nonatomic,strong) UIView *panel;
@property(nonatomic,strong) UIButton *entry,*apply,*reset;
@property(nonatomic,strong) UILabel *value,*status;
@property(nonatomic,strong) UISlider *slider;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic) NSInteger draft;
@property(nonatomic) uint64_t epoch;
@property(nonatomic) uint64_t lastOpenSeq;
@end

@implementation PCPRatePanel
- (UIButton *)button:(NSString *)title action:(SEL)action {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    b.titleLabel.font=[UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
    [b setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    b.backgroundColor=[UIColor colorWithRed:0.32 green:0.26 blue:0.56 alpha:1];
    b.layer.cornerRadius=12;
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return b;
}
- (UILabel *)label:(CGFloat)size {
    UILabel *l=[UILabel new];l.textColor=UIColor.whiteColor;
    l.font=[UIFont systemFontOfSize:size weight:UIFontWeightMedium];
    l.textAlignment=NSTextAlignmentCenter;return l;
}
- (void)build {
    self.overlay=[[PCPRateOverlay alloc] initWithFrame:CGRectZero];
    self.overlay.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    self.entry=[self button:@"倍率" action:@selector(toggle)];
    [self.overlay addSubview:self.entry];
    self.panel=[UIView new];self.panel.hidden=YES;self.panel.layer.cornerRadius=20;
    self.panel.backgroundColor=[UIColor colorWithRed:0.10 green:0.08 blue:0.20 alpha:0.98];
    [self.overlay addSubview:self.panel];
    UILabel *title=[self label:19];title.text=@"练习倍率";title.frame=CGRectMake(20,14,320,28);
    [self.panel addSubview:title];
    self.value=[self label:34];self.value.font=[UIFont monospacedDigitSystemFontOfSize:34 weight:UIFontWeightBold];
    self.value.frame=CGRectMake(20,49,320,44);[self.panel addSubview:self.value];
    self.slider=[[UISlider alloc] initWithFrame:CGRectMake(28,100,304,38)];
    self.slider.minimumValue=50;self.slider.maximumValue=250;self.slider.continuous=YES;
    self.slider.tintColor=[UIColor colorWithRed:0.73 green:0.63 blue:1 alpha:1];
    self.slider.accessibilityLabel=@"练习倍率，0.50 至 2.50 倍";
    [self.slider addTarget:self action:@selector(slide:) forControlEvents:UIControlEventValueChanged];
    [self.panel addSubview:self.slider];
    UILabel *range=[self label:12];range.text=@"0.50x                                      2.50x";
    range.frame=CGRectMake(28,134,304,20);[self.panel addSubview:range];
    NSArray *names=@[@"− 0.05",@"− 0.01",@"+ 0.01",@"+ 0.05"];
    int steps[]={-5,-1,1,5};
    for(int i=0;i<4;i++) {
        UIButton *b=[self button:names[i] action:@selector(step:)];b.tag=steps[i];
        CGFloat h=(i==0||i==3)?52:44;
        b.frame=CGRectMake(16+i*84,160+(52-h)/2,76,h);[self.panel addSubview:b];
    }
    self.status=[self label:13];self.status.numberOfLines=2;
    self.status.frame=CGRectMake(16,218,328,40);[self.panel addSubview:self.status];
    self.reset=[self button:@"恢复 1.00x" action:@selector(resetRate)];
    self.reset.frame=CGRectMake(16,267,158,48);[self.panel addSubview:self.reset];
    self.apply=[self button:@"应用" action:@selector(applyRate)];
    self.apply.backgroundColor=[UIColor colorWithRed:0.48 green:0.36 blue:0.82 alpha:1];
    self.apply.frame=CGRectMake(186,267,158,48);[self.panel addSubview:self.apply];
    UIButton *close=[self button:@"收起" action:@selector(toggle)];
    close.frame=CGRectMake(130,326,100,38);[self.panel addSubview:close];
    self.draft=(NSInteger)pcp_rate_ui_stored_percent();[self updateDraft];
}
- (void)updateDraft {
    self.draft=MAX(50,MIN(250,self.draft));
    self.value.text=[NSString stringWithFormat:@"%.2fx",self.draft/100.0];
    self.slider.value=(float)self.draft;
    self.slider.accessibilityValue=self.value.text;
}
- (void)slide:(UISlider *)sender {self.draft=lroundf(sender.value);[self updateDraft];}
- (void)step:(UIButton *)sender {self.draft+=sender.tag;[self updateDraft];}
- (void)toggle {self.panel.hidden=!self.panel.hidden;}
- (void)applyRate {
    if(pcp_rate_ui_request((unsigned)self.draft,self.epoch)) {
        [[NSUserDefaults standardUserDefaults] setInteger:self.draft forKey:@"PCPPracticeRatePercent"];
        [self tick];
    } else {self.status.text=@"状态已变化，请保持暂停后重试";}
}
- (void)resetRate {self.draft=100;[self updateDraft];[self applyRate];}
- (UIWindow *)gameWindow {
    UIApplication *app=UIApplication.sharedApplication;
    for(UIScene *scene in app.connectedScenes) {
        if(scene.activationState!=UISceneActivationStateForegroundActive || ![scene isKindOfClass:UIWindowScene.class]) continue;
        for(UIWindow *w in ((UIWindowScene *)scene).windows)
            if(w.isKeyWindow && !w.hidden && w.windowLevel==UIWindowLevelNormal) return w;
    }
    // Legacy lifecycle client fallback. Does not create or change key windows.
    for(UIWindow *w in app.windows) if(w.isKeyWindow && !w.hidden && w.windowLevel==UIWindowLevelNormal) return w;
    return nil;
}
- (void)tick {
    PracticeRateUIState state=pcp_rate_ui_state();
    UIWindow *window=[self gameWindow];
    if(!window || !state.visible || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive) {
        self.overlay.hidden=YES;self.panel.hidden=YES;return;
    }
    if(self.overlay.superview!=window) { [self.overlay removeFromSuperview];[window addSubview:self.overlay]; }
    self.overlay.frame=window.bounds;[window bringSubviewToFront:self.overlay];self.overlay.hidden=NO;
    if(state.visible&&state.open_seq!=self.lastOpenSeq){self.lastOpenSeq=state.open_seq;self.panel.hidden=NO;}
    if(self.epoch!=state.epoch) self.panel.hidden=YES;
    self.epoch=state.epoch;
    UIEdgeInsets safe=window.safeAreaInsets;CGSize size=window.bounds.size;
    self.entry.frame=CGRectMake(size.width-safe.right-150,safe.top+16,134,46);
    self.panel.transform=CGAffineTransformIdentity;
    self.panel.bounds=CGRectMake(0,0,360,378);
    CGFloat scale=MIN(1,MIN((size.width-safe.left-safe.right-24)/360,(size.height-safe.top-safe.bottom-24)/378));
    self.panel.transform=CGAffineTransformMakeScale(MAX(0.5,scale),MAX(0.5,scale));
    self.panel.center=CGPointMake(size.width/2,size.height/2);
    [self.entry setTitle:[NSString stringWithFormat:@"倍率 %.2fx",state.applied/100.0] forState:UIControlStateNormal];
    self.apply.enabled=self.reset.enabled=state.ready&&!state.pending;
    self.apply.alpha=self.reset.alpha=self.apply.enabled?1:0.45;
    self.slider.enabled=!state.pending;
    self.status.text=state.pending?@"正在应用…":state.result==-3?@"控制已停止，请退出并重新启动":state.result==-2?@"未能应用，请重新暂停后重试":
        state.prep&&state.ready?[NSString stringWithFormat:@"已设定 %.2fx\n开始播放后生效",state.applied/100.0]:
        !state.ready?@"等待暂停状态就绪…":
        [NSString stringWithFormat:@"当前 %.2fx · 调整后点击应用\n音乐与谱面同步变速",state.applied/100.0];
}
@end

unsigned pcp_rate_ui_stored_percent(void)
{
    NSInteger saved=[[NSUserDefaults standardUserDefaults] integerForKey:@"PCPPracticeRatePercent"];
    return (saved>=50&&saved<=250)?(unsigned)saved:100u;
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
