/* Shared panel implementation. Compiled inside practice_rate_ui.m. */
#include "practice_design_skin.inc"
static void PTRegisterFonts(void) {
    for(NSString *name in @[@"Fonts/Exo-Light.ttf",@"Fonts/Exo-Regular.ttf",@"Fonts/Exo-Medium.ttf",@"Fonts/Exo-SemiBold.ttf",@"Fonts/NotoSansCJKsc-Light.otf",@"GoogleSignIn_GoogleSignIn.bundle/Roboto-Bold.ttf"]) {
        NSString *path=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:name];
        if([NSFileManager.defaultManager fileExistsAtPath:path])
            CTFontManagerRegisterFontsForURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],kCTFontManagerScopeProcess,NULL);
    }
}
static UIFont *PTFont(CGFloat size,BOOL latin) {
    UIFont *font=[UIFont fontWithName:latin?@"Exo-Light":@"Exo-Regular" size:size];
    return font?:[UIFont systemFontOfSize:size weight:UIFontWeightLight];
}
static NSString *PTTime(unsigned ms) {
    char text[32],detail[32];pp_format(ms,text,sizeof(text),detail,sizeof(detail));
    return [NSString stringWithUTF8String:text];
}
static NSString *PTName(NSUInteger i) {return @[@"A",@"B",@"T1",@"T2",@"T3"][i];}
@interface PCPRateOverlay : UIView @end
@implementation PCPRateOverlay
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e {
    UIView *hit=[super hitTest:p withEvent:e];return hit==self?nil:hit;
}
@end
@interface PTBackdrop : UIView @end
@implementation PTBackdrop
- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])) {
        self.backgroundColor=[UIColor colorWithRed:0.13 green:0.10 blue:0.29 alpha:1];
        self.opaque=YES;self.contentMode=UIViewContentModeRedraw;
    }return self;
}
- (void)layoutSubviews {[super layoutSubviews];[self setNeedsDisplay];}
- (void)drawRect:(CGRect)rect {
    CGContextRef c=UIGraphicsGetCurrentContext();
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGFloat colors[]={0.14,0.10,0.31,1, 0.25,0.18,0.46,1, 0.13,0.10,0.29,1};
    CGFloat stops[]={0,0.5,1};CGGradientRef g=CGGradientCreateWithColorComponents(space,colors,stops,3);
    CGContextDrawLinearGradient(c,g,CGPointZero,CGPointMake(0,rect.size.height),0);CGGradientRelease(g);CGColorSpaceRelease(space);
    CGContextSetStrokeColorWithColor(c,[UIColor colorWithRed:0.77 green:0.67 blue:1 alpha:0.12].CGColor);
    CGContextSetLineWidth(c,1);
    for(int i=0;i<9;i++) {
        CGFloat x=rect.size.width/2+(i-4)*32,y=rect.size.height*0.48,h=42+(i%3)*24;
        CGContextMoveToPoint(c,x,y-h);CGContextAddLineToPoint(c,x+25,y);
        CGContextAddLineToPoint(c,x,y+h);CGContextAddLineToPoint(c,x-25,y);CGContextClosePath(c);CGContextStrokePath(c);
    }
}
@end
@interface PTHexButton : UIButton @end
@implementation PTHexButton
- (void)drawRect:(CGRect)rect {
    CGRect r=CGRectInset(self.bounds,1,1);CGFloat k=12;
    UIBezierPath *p=[UIBezierPath bezierPath];
    [p moveToPoint:CGPointMake(r.origin.x+k,r.origin.y)];
    [p addLineToPoint:CGPointMake(CGRectGetMaxX(r)-k,r.origin.y)];
    [p addLineToPoint:CGPointMake(CGRectGetMaxX(r),CGRectGetMidY(r))];
    [p addLineToPoint:CGPointMake(CGRectGetMaxX(r)-k,CGRectGetMaxY(r))];
    [p addLineToPoint:CGPointMake(r.origin.x+k,CGRectGetMaxY(r))];
    [p addLineToPoint:CGPointMake(r.origin.x,CGRectGetMidY(r))];[p closePath];
    [[UIColor colorWithRed:0.38 green:0.30 blue:0.59 alpha:self.selected?0.8:0.35] setFill];[p fill];
    [[UIColor colorWithWhite:1 alpha:self.selected?1:0.65] setStroke];p.lineWidth=self.selected?2:1;[p stroke];
}
- (void)setEnabled:(BOOL)v {[super setEnabled:v];self.alpha=v?1:0.32;[self setNeedsDisplay];}
- (void)setSelected:(BOOL)s {[super setSelected:s];[self setNeedsDisplay];}
@end
@interface PTTimeline : UIView
@property(nonatomic) PracticePoints points;
@property(nonatomic) unsigned extent,current;
@property(nonatomic) NSInteger selected;
@property(nonatomic,copy) void (^choose)(NSUInteger);
@property(nonatomic,copy) void (^drag)(unsigned);
@property(nonatomic) BOOL drafting;
@property(nonatomic) unsigned draftMs,draftLimit;
@property(nonatomic) NSInteger draftID;
@property(nonatomic) CGPoint touchOrigin;
@property(nonatomic) BOOL touchMayDrag,touchDragged;
@property(nonatomic,strong) UIImageView *currentStar;
@end
@implementation PTTimeline
- (CGFloat)xFor:(unsigned)ms {return 20+(self.bounds.size.width-40)*MIN(1.0,ms/(double)MAX(1u,self.extent));}
- (void)drawText:(NSString *)text x:(CGFloat)x y:(CGFloat)y color:(UIColor *)color {
    NSDictionary *attrs=@{NSFontAttributeName:([UIFont fontWithName:@"Exo-Medium" size:18]?:PTFont(18,YES)),NSForegroundColorAttributeName:color};
    CGFloat width=[text sizeWithAttributes:attrs].width;
    [text drawAtPoint:CGPointMake(MAX(0,MIN(self.bounds.size.width-width,x-width/2)),y) withAttributes:attrs];
}
- (UIImage *)skin:(NSString *)key selected:(BOOL)selected {
    static NSMutableDictionary *cache; if(!cache)cache=[NSMutableDictionary dictionary];
    NSString *cacheKey=[key stringByAppendingFormat:@"-%d",selected];UIImage *image=cache[cacheKey];
    if(!image){NSDictionary *e=PTElement(key);image=PTDesignImage(e,selected?(e[@"selectedAsset"]?:e[@"asset"]):e[@"asset"]);if(image)cache[cacheKey]=image;}
    return image;
}
- (void)drawRect:(CGRect)rect {
    CGContextRef c=UIGraphicsGetCurrentContext();CGFloat width=self.bounds.size.width,y=84;
    [[self skin:@"el_cfd13949" selected:NO] drawInRect:CGRectMake(0,y-12,width,24)];
    if((self.points.mask&3)==3 && self.points.ms[1]>self.points.ms[0]){
        [[UIColor colorWithRed:0.65 green:0.5 blue:1 alpha:0.18] setFill];
        UIRectFill(CGRectMake([self xFor:self.points.ms[0]],y-6,[self xFor:self.points.ms[1]]-[self xFor:self.points.ms[0]],12));
    }
    CGContextSetStrokeColorWithColor(c,[UIColor colorWithWhite:1 alpha:0.6].CGColor);CGContextSetLineWidth(c,1);
    for(int i=0;i<=4;i++){CGFloat x=20+(width-40)*i/4;CGContextMoveToPoint(c,x,y-4);CGContextAddLineToPoint(c,x,y+4);CGContextStrokePath(c);[self drawText:PTTime((unsigned)((uint64_t)self.extent*i/4)) x:x y:116 color:UIColor.whiteColor];}
    for(unsigned i=0;i<5;i++)if(self.points.mask&(1u<<i)){
        CGFloat x=[self xFor:self.drafting && self.draftID==(NSInteger)i?self.draftMs:self.points.ms[i]],labelY=i*15;
        CGContextSetStrokeColorWithColor(c,[UIColor colorWithWhite:1 alpha:0.6].CGColor);CGContextMoveToPoint(c,x,labelY+20);CGContextAddLineToPoint(c,x,y);CGContextStrokePath(c);
        BOOL selected=self.selected==(NSInteger)i;CGFloat size=i<2?48:40;
        [[self skin:@[@"el_e415ec13",@"el_3538d2fa",@"el_ff77c1c7",@"el_0aba565b",@"el_ae410b23"][i] selected:selected] drawInRect:CGRectMake(x-size/2,y-size/2,size,size)];
        [self drawText:[NSString stringWithFormat:@"%@ %@",PTName(i),PTTime(self.drafting && self.draftID==(NSInteger)i?self.draftMs:self.points.ms[i])] x:x y:labelY color:selected?[UIColor colorWithRed:0.8 green:0.9 blue:1 alpha:1]:UIColor.whiteColor];
    }
    unsigned currentMs=self.drafting && self.draftID==5?self.draftMs:self.current;
    CGFloat x=[self xFor:currentMs];
    if(!self.currentStar){self.currentStar=[[UIImageView alloc] initWithImage:[self skin:@"el_6680a60d" selected:NO]];self.currentStar.userInteractionEnabled=NO;self.clipsToBounds=NO;[self addSubview:self.currentStar];}
    self.currentStar.frame=CGRectMake(x-48,y-48,96,96);
    [self drawText:[NSString stringWithFormat:@"C %@",PTTime(currentMs)] x:x y:94 color:[UIColor colorWithRed:0.5 green:0.95 blue:0.87 alpha:1]];
}
- (NSInteger)pointAt:(CGPoint)p {
    if(self.drafting && self.draftID==5 && p.y<=110 && fabs(p.x-[self xFor:self.draftMs])<48)return 5;
    if(p.y<75){for(unsigned i=0;i<5;i++)if(self.points.mask&(1u<<i)){
        unsigned ms=self.drafting && self.draftID==(NSInteger)i?self.draftMs:self.points.ms[i];
        NSString *label=[NSString stringWithFormat:@"%@ %@",PTName(i),PTTime(ms)];
        CGFloat width=[label sizeWithAttributes:@{NSFontAttributeName:([UIFont fontWithName:@"Exo-Medium" size:18]?:PTFont(18,YES))}].width;
        CGFloat left=MAX(0,MIN(self.bounds.size.width-width,[self xFor:ms]-width/2));
        if(CGRectContainsPoint(CGRectMake(left,i*15,width,18),p))return (NSInteger)i;
    }}
    if(p.y<=110){NSInteger best=-1;CGFloat distance=32;
        for(unsigned i=0;i<5;i++)if(self.points.mask&(1u<<i)){
            unsigned ms=self.drafting && self.draftID==(NSInteger)i?self.draftMs:self.points.ms[i];
            CGFloat d=fabs(p.x-[self xFor:ms]);if(d<distance || (d==distance && self.selected==(NSInteger)i)){distance=d;best=(NSInteger)i;}
        }return best;
    }
    return -1;
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    CGPoint p=[touches.anyObject locationInView:self];NSInteger point=[self pointAt:p];
    self.touchOrigin=p;self.touchDragged=NO;self.touchMayDrag=point>=0;
    if(point>=0 && point<5 && (!self.drafting || self.draftID!=point) && self.choose)self.choose((NSUInteger)point);
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if(!self.drafting || !self.touchMayDrag || !self.drag)return;
    CGPoint p=[touches.anyObject locationInView:self];
    if(!self.touchDragged && hypot(p.x-self.touchOrigin.x,p.y-self.touchOrigin.y)<8)return;
    self.touchDragged=YES;CGFloat x=p.x;
    double ratio=MAX(0,MIN(1,(x-20)/MAX(1,self.bounds.size.width-40)));
    uint64_t ms=(uint64_t)llround(ratio*self.extent/100.0)*100;
    ms=MIN(ms,(uint64_t)(self.draftLimit/100)*100);
    self.drag((unsigned)ms);
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    self.touchMayDrag=NO;self.touchDragged=NO;
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {self.touchMayDrag=NO;self.touchDragged=NO;}

@end

@interface PCPRatePanel : NSObject {
    PracticePoints _edit;
}
@property(nonatomic,strong) PCPRateOverlay *overlay,*outsideEntryHost;
@property(nonatomic,strong) UIView *outsidePanel;
@property(nonatomic,strong) UIButton *outsideEntry;
@property(nonatomic,strong) UILabel *outsideStatus;
@property(nonatomic,strong) NSMutableDictionary<NSString *,UIView *> *outsideViews;
@property(nonatomic) uint64_t outsideContext;
@property(nonatomic) NSTimeInterval outsideSince,outsideMessageUntil;
@property(nonatomic,strong) UIView *panel,*editBox,*curtain;
@property(nonatomic,strong) UIButton *entry,*apply,*reset,*noteMode,*pitchMode,*hudMode,*loop,*jump,*addT,*removeT,*close,*continueButton,*cancelJump;
@property(nonatomic,strong) NSMutableArray<UIButton *> *chips,*editButtons,*captureButtons,*editCommitButtons;
@property(nonatomic,strong) UILabel *value,*status,*selection,*rangeLabel,*compLabel,*speedHUD,*accHUD,*noteSpeeds,*countdown;
@property(nonatomic,strong) UILabel *noteSpeedValue,*appliedValue,*currentHUD,*durationHUD;
@property(nonatomic,strong) NSMutableArray<UIView *> *hudProgressViews;
@property(nonatomic,strong) UIView *hudProgressThumb;
@property(nonatomic) unsigned hudCurrentMs,hudDurationMs;
@property(nonatomic) BOOL hudRateExpanded;
@property(nonatomic,strong) NSMutableArray<UIButton *> *rateSteps;
@property(nonatomic,strong) UILabel *noteControlValue;
@property(nonatomic,strong) UISlider *noteSlider;
@property(nonatomic,strong) NSMutableArray<UIButton *> *noteSteps;
@property(nonatomic,strong) UIButton *previousPoint,*nextPoint,*freeSeek,*heldButton;
@property(nonatomic,strong) NSTimer *holdTimer;
@property(nonatomic) BOOL autoRateQueued,freeEditing;
@property(nonatomic) NSTimeInterval autoRateDue;
@property(nonatomic) uint64_t autoRateEpoch;
@property(nonatomic,strong) UISlider *slider;
@property(nonatomic,strong) UILabel *milliseconds;
@property(nonatomic,strong) PTTimeline *axis;
@property(nonatomic,strong) NSTimer *timer,*countdownTimer;
@property(nonatomic) NSInteger draft,selected;
@property(nonatomic) uint64_t epoch,lastOpenSeq,pointEpoch;
@property(nonatomic) NSTimeInterval messageUntil;
@end
@implementation PCPRatePanel
- (UILabel *)label:(CGFloat)size frame:(CGRect)frame text:(NSString *)text {
    UILabel *v=[[UILabel alloc] initWithFrame:frame];v.textColor=UIColor.whiteColor;
    v.font=PTFont(size,NO);v.text=text;v.textAlignment=NSTextAlignmentCenter;
    [self.panel addSubview:v];return v;
}
- (UIButton *)button:(NSString *)title frame:(CGRect)frame action:(SEL)action {
    PTHexButton *v=[[PTHexButton alloc] initWithFrame:frame];v.backgroundColor=UIColor.clearColor;
    [v setTitle:title forState:UIControlStateNormal];[v setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    v.titleLabel.font=[UIFont fontWithName:@"Exo-Medium" size:17]?:PTFont(17,NO);v.titleLabel.numberOfLines=2;v.titleLabel.textAlignment=NSTextAlignmentCenter;
    [v addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];[self.panel addSubview:v];return v;
}
- (void)message:(NSString *)s {self.status.text=s;self.messageUntil=CFAbsoluteTimeGetCurrent()+3;}
- (BOOL)hudEnabled {
    id value=[NSUserDefaults.standardUserDefaults objectForKey:@"PCPHUDVisible"];
    return value?[value boolValue]:YES;
}
- (NSArray<UIView *> *)hudViews {
    NSMutableArray<UIView *> *views=[NSMutableArray arrayWithObjects:self.speedHUD,self.accHUD,self.currentHUD,self.durationHUD,nil];
    if(self.hudProgressViews)[views addObjectsFromArray:self.hudProgressViews];return views;
}
- (void)setHudHidden:(BOOL)hidden {
    for(UIView *view in [self hudViews])view.hidden=hidden;
    for(UIView *view in self.hudProgressViews)view.hidden=hidden||!self.hudRateExpanded;
}
- (void)speedTap {
    if(pcp_paused_fast() || pcp_rate_ui_state().visible || !self.panel.hidden)return;
    self.hudRateExpanded=!self.hudRateExpanded;[self tick];
}
- (void)hudTap {
    BOOL enabled=![self hudEnabled];[NSUserDefaults.standardUserDefaults setBool:enabled forKey:@"PCPHUDVisible"];
    NSString *message=enabled?@"HUD 已开启":@"HUD 已关闭";
    if(self.outsidePanel && !self.outsidePanel.hidden)[self outsideMessage:message];else [self message:message];
    [self outsideRefresh];[self tick];
}
#include "practice_design_panel.inc"
#include "practice_outside_ui.inc"
- (void)hideForTransition {
    self.hudRateExpanded=NO;self.autoRateQueued=NO;[self cancelEdit];self.panel.hidden=self.curtain.hidden=self.entry.hidden=YES;
    [self setHudHidden:YES];self.countdown.hidden=YES;
    self.lastOpenSeq=pcp_rate_ui_state().open_seq;
}
- (void)continuePlaying {if(!pcp_native_continue())[self message:@"请返回暂停菜单后继续播放"];}
- (void)cancelWaiting {if(pcp_cancel_jump())[self message:@"已取消待执行跳转，可重新设置"];}
- (void)countdownTick {
    if(UIApplication.sharedApplication.applicationState!=UIApplicationStateActive || !self.panel.hidden || self.overlay.hidden) {self.countdown.hidden=YES;if(!self.panel.hidden)[self setHudHidden:YES];return;}
    BOOL paused=pcp_paused_fast() || pcp_rate_ui_state().visible;
    for(UIView *v in [self hudViews])v.alpha=paused?0.35:1;
    for(UIView *v in self.hudProgressViews)v.alpha=0.32;
    unsigned playback=0;if(!self.currentHUD.hidden && pcp_playback_position(&playback)){self.hudCurrentMs=playback;self.currentHUD.text=PTTime(playback);[self layoutDesignHUD];}
    PracticeCountdownState c=pcp_countdown_state();self.countdown.hidden=!c.active||c.paused||paused;
    if(self.countdown.hidden)return;
    [self layoutDesignHUD];
    self.countdown.text=[NSString stringWithFormat:@"%u:%03u",c.remaining_ms/1000,c.remaining_ms%1000];
}
- (void)updateDraft {self.draft=MAX(50,MIN(200,self.draft));self.value.text=[NSString stringWithFormat:@"%.2fx",self.draft/100.0];self.slider.value=self.draft;}
- (void)slide:(UISlider *)s {self.draft=lroundf(s.value);[self updateDraft];[self queueAutoRate];}
- (void)step:(UIButton *)s {self.draft+=s.tag;[self updateDraft];[self queueAutoRate];}
- (void)toggle { [self cancelEdit];self.panel.hidden=!self.panel.hidden;if(!self.panel.hidden){[self setHudHidden:YES];self.draft=pcp_rate_ui_stored_percent();[self updateDraft];} }
- (void)applyRate {
    if(pcp_rate_ui_request((unsigned)self.draft,self.epoch)){pcp_rate_ui_save_percent((unsigned)self.draft);[self message:(pcp_rate_ui_state().prep?@"倍率已保存，将在开始播放时应用":@"倍率请求已提交")];}else [self message:@"暂停状态尚未就绪"];
}
- (void)resetRate {self.draft=100;[self updateDraft];[self applyRate];}
- (void)noteTap {pcp_note_mode_set(!pcp_note_mode_get());[self message:@"Note 模式将在重试或重新进歌后生效"];} 
- (void)pitchTap {pcp_pitch_mode_set(!pcp_pitch_mode_get());}
- (void)comp:(UIButton *)b {if(pcp_seek_phase()>=1 && pcp_seek_phase()<=4)return;NSInteger v=(NSInteger)pcp_pitch_comp_get()+b.tag;pcp_pitch_comp_set((unsigned)MAX(0,MIN(80,v)));}
- (void)capture:(UIButton *)b {[self cancelEdit];if(pcp_point_capture((unsigned)b.tag)){[self choose:(NSUInteger)b.tag];[self message:@"已用当前播放位置设点"];}else [self message:@"当前无法设点"];} 
- (NSInteger)bookmarkTarget {
    PracticePoints p=pcp_points_snapshot();
    if(!self.freeEditing && self.selected>=2 && self.selected<5)return self.selected;
    for(int i=2;i<5;i++)if(!(p.mask&(1u<<i)))return i;
    return -1;
}
- (void)addBookmark {
    NSInteger target=[self bookmarkTarget];
    if(target<0){[self message:@"T 点已满，选中一个 T 点后可用 C 覆盖"];return;}
    if(!pcp_point_capture((unsigned)target)){[self message:@"当前无法读取 C 点设点"];return;}
    /* Capturing another slot must not consume the editor's unsaved draft. */
    if(self.selected==target || !_edit.editing)[self choose:(NSUInteger)target];
    [self message:[NSString stringWithFormat:@"已用实际 C 设置 %@",PTName(target)]];
}
- (void)deletePoint {if(!self.freeEditing && self.selected>=0 && pcp_point_delete((unsigned)self.selected)){[self cancelEdit];self.selected=-1;[self message:@"已删除选中点"];}}
- (void)choose:(NSUInteger)i {
    if(i>=5 || (pcp_seek_phase()>=1 && pcp_seek_phase()<=4))return;
    [self cancelEdit];self.selected=(NSInteger)i;
    PracticePoints p=pcp_points_snapshot();
    if(!(p.mask&(1u<<i))){[self message:@"空点位，先使用设置按钮记录当前时间"];return;}
    _edit=p;_edit.selected=(int)i;_edit.editing=0;_edit.duration=p.duration?p.duration-1:pcp_points_extent();
    if(pp_select(&_edit,(unsigned)i)!=2)return;
    self.milliseconds.text=[NSString stringWithFormat:@"%u",_edit.draft];
    self.editBox.hidden=NO;
    self.axis.drafting=YES;self.axis.draftID=self.selected;self.axis.draftMs=_edit.draft;self.axis.draftLimit=_edit.duration;
}
- (void)cyclePoint:(UIButton *)button {
    NSInteger base=self.freeEditing?5:self.selected;
    NSInteger next=base<0?(button.tag<0?5:0):(base+button.tag+6)%6;
    if(next==5)[self freeSeekTap];else [self choose:(NSUInteger)next];
}
- (void)freeSeekTap {
    if(pcp_seek_phase()>=1 && pcp_seek_phase()<=4)return;
    [self cancelEdit];self.freeEditing=YES;
    PracticePoints p=pcp_points_snapshot();pp_init(&_edit,p.duration?p.duration-1:pcp_points_extent());
    _edit.editing=1;_edit.draft=MIN(pcp_points_current(),_edit.duration);
    self.milliseconds.text=[NSString stringWithFormat:@"%u",_edit.draft];self.editBox.hidden=NO;
    self.axis.drafting=YES;self.axis.draftID=5;self.axis.draftMs=_edit.draft;self.axis.draftLimit=_edit.duration;
}
- (void)dragTime:(unsigned)ms {
    if(!_edit.editing || !pp_input(&_edit,ms))return;
    self.milliseconds.text=[NSString stringWithFormat:@"%u",ms];self.axis.draftMs=ms;[self.axis setNeedsDisplay];
}
- (void)stopHold {[self.holdTimer invalidate];self.holdTimer=nil;self.heldButton=nil;}
- (void)repeatTime {
    if(!self.heldButton.enabled || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive){[self stopHold];return;}
    [self timeStep:self.heldButton];
}
- (void)holdTime:(UILongPressGestureRecognizer *)gesture {
    if(gesture.state==UIGestureRecognizerStateBegan){[self stopHold];self.heldButton=(UIButton *)gesture.view;[self repeatTime];self.holdTimer=[NSTimer timerWithTimeInterval:0.08 target:self selector:@selector(repeatTime) userInfo:nil repeats:YES];[[NSRunLoop mainRunLoop] addTimer:self.holdTimer forMode:NSRunLoopCommonModes];}
    else if(gesture.state==UIGestureRecognizerStateEnded || gesture.state==UIGestureRecognizerStateCancelled || gesture.state==UIGestureRecognizerStateFailed)[self stopHold];
}
- (void)queueAutoRate {self.autoRateQueued=YES;self.autoRateDue=CFAbsoluteTimeGetCurrent()+0.12;self.autoRateEpoch=self.epoch;}
- (void)flushAutoRate {
    PracticeRateUIState state=pcp_rate_ui_state();
    if(UIApplication.sharedApplication.applicationState!=UIApplicationStateActive || state.transitioning){self.autoRateQueued=NO;return;}
    if(self.autoRateEpoch!=state.epoch || !state.visible){self.autoRateQueued=NO;return;}
    if(self.autoRateQueued && CFAbsoluteTimeGetCurrent()>=self.autoRateDue && state.ready && !state.pending){
        if(pcp_rate_ui_request((unsigned)self.draft,state.epoch)){pcp_rate_ui_save_percent((unsigned)self.draft);self.autoRateQueued=NO;}
    }
}
- (double)noteDesired {
    PracticeHUDState h=pcp_hud_state();unsigned reference=pcp_note_reference_get();
    double base=reference?reference/1000.0:h.native_note_speed;
    return base*(pcp_note_mode_get()?100.0/pcp_rate_ui_stored_percent():1.0);
}
- (void)saveNote:(double)value {
    if(pcp_note_mode_get() || !self.noteSlider.enabled)return;
    pcp_note_current_set(MAX(0.5,MIN(30.0,value)));
    [self message:@"CNS 已保存，重试或重新进歌后生效"];[self tick];
}
- (void)noteStep:(UIButton *)button {[self saveNote:[self noteDesired]+button.tag/100.0];}
- (void)noteSlide:(UISlider *)slider {[self saveNote:round(slider.value*10.0)/10.0];}
- (void)noteNative:(UILongPressGestureRecognizer *)gesture {
    if(gesture.state!=UIGestureRecognizerStateBegan || pcp_note_mode_get() || !self.noteSlider.enabled)return;
    pcp_note_native_reset();[self message:@"已恢复跟随游戏流速，重试生效"];
}
- (void)chip:(UIButton *)b {[self choose:(NSUInteger)b.tag];}
- (void)timeStep:(UIButton *)b {
    unsigned phase=pcp_seek_phase();if(phase>=1 && phase<=4)return;
    if(!_edit.editing && self.selected>=0)[self choose:(NSUInteger)self.selected];
    if(!_edit.editing)return;
    pp_adjust(&_edit,(int)b.tag);self.milliseconds.text=[NSString stringWithFormat:@"%u",_edit.draft];
    self.axis.draftMs=_edit.draft;[self.axis setNeedsDisplay];
}
- (void)confirmEdit {
    if(self.freeEditing){
        if(!_edit.editing || !pcp_jump_time(_edit.draft)){[self message:@"无法提交定位，请确认时间范围与暂停状态"];return;}
        [self hideForTransition];return;
    }
    if(!_edit.editing || !pcp_point_update((unsigned)self.selected,_edit.draft)){[self message:@"时间超出范围或会话已变化，尚未保存"];return;}
    [self cancelEdit];[self message:@"时间已更新"];
}
- (void)cancelEdit {[self stopHold];self.freeEditing=NO;self.axis.drafting=NO;_edit.editing=0;self.editBox.hidden=NO;self.jump.hidden=self.selection.hidden=NO;}
- (void)jumpTap {
    if(_edit.editing) {
        if(!_edit.editing || !pcp_jump_time(_edit.draft)){[self message:@"编辑时间无效或暂时不能跳转"];return;}
        /* Preview-jump to the draft; only Confirm writes the saved point. */
        [self hideForTransition];return;
    }
    if(self.selected>=0 && pcp_point_jump((unsigned)self.selected)){[self hideForTransition];}
    else [self message:@"无法提交跳转，请保持暂停并选中点位"];
}
- (void)loopTap {
    int enabled=pcp_loop_suspended()?1:!pcp_ab_loop_get();pcp_ab_loop_set(enabled);
    [self message:enabled?@"A/B 循环已开启":@"A/B 循环已关闭"];
}
- (UIWindow *)gameWindow {
    for(UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if(scene.activationState==UISceneActivationStateForegroundActive && [scene isKindOfClass:UIWindowScene.class])
            for(UIWindow *w in ((UIWindowScene *)scene).windows)if(w.isKeyWindow && !w.hidden && w.windowLevel==UIWindowLevelNormal)return w;
    for(UIWindow *w in UIApplication.sharedApplication.windows)if(w.isKeyWindow && !w.hidden && w.windowLevel==UIWindowLevelNormal)return w;
    return nil;
}
- (void)tick {
    [self flushAutoRate];PracticeRateUIState s=pcp_rate_ui_state();UIWindow *w=[self gameWindow];
    if(!w || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive){[self hideForTransition];self.outsidePanel.hidden=self.outsideEntryHost.hidden=YES;self.outsideContext=0;self.overlay.hidden=YES;return;}
    if(self.overlay.superview!=w){[self.overlay removeFromSuperview];[w addSubview:self.overlay];}
    self.overlay.frame=w.bounds;[w bringSubviewToFront:self.overlay];self.overlay.hidden=NO;
    if([self updateOutside:w])return;
    if(s.transitioning){[self hideForTransition];self.overlay.hidden=YES;return;}
    PracticeHUDState hud=pcp_hud_state();
    if(hud.scroll_valid && fabs([NSUserDefaults.standardUserDefaults doubleForKey:@"PCPLastNativeNote"]-hud.native_note_speed)>0.0001)
        [NSUserDefaults.standardUserDefaults setDouble:hud.native_note_speed forKey:@"PCPLastNativeNote"];
    BOOL hudHidden=!hud.visible || ![self hudEnabled];[self setHudHidden:hudHidden];
    self.speedHUD.text=[NSString stringWithFormat:@"SPEED %.2fx",hud.percent/100.0];
    self.accHUD.text=hud.score_valid?[NSString stringWithFormat:@"ACC %.2f%%",hud.accuracy]:@"ACC --";
    unsigned currentPosition=0;self.currentHUD.text=(hud.visible && pcp_playback_position(&currentPosition))?PTTime(currentPosition):@"--:--.---";
    PracticePoints hudPoints=pcp_points_snapshot();self.durationHUD.text=hudPoints.duration?PTTime(hudPoints.duration):@"--:--.---";
    self.hudCurrentMs=currentPosition;self.hudDurationMs=hudPoints.duration;
    [self layoutDesignHUD];
    for(UIView *view in [self hudViews])view.alpha=(s.visible||pcp_paused_fast())?0.35:1;
    for(UIView *view in self.hudProgressViews)view.alpha=0.32;
    self.entry.hidden=YES; /* Native pause row owns the entry. */
    self.entry.frame=CGRectMake(w.bounds.size.width-w.safeAreaInsets.right-162,w.safeAreaInsets.top+12,150,40);
    [self.entry setTitle:[NSString stringWithFormat:@"倍率 %.2fx",s.applied/100.0] forState:UIControlStateNormal];
    if(!s.visible){self.panel.hidden=YES;self.curtain.hidden=YES;[self setHudHidden:hudHidden];[self cancelEdit];return;}
    if(self.epoch!=s.epoch){self.panel.hidden=YES;[self cancelEdit];}self.epoch=s.epoch;
    if(s.open_seq!=self.lastOpenSeq){self.lastOpenSeq=s.open_seq;self.panel.hidden=NO;self.draft=pcp_rate_ui_stored_percent();[self updateDraft];}
    if(self.pointEpoch!=pcp_points_epoch()){self.pointEpoch=pcp_points_epoch();self.selected=-1;[self cancelEdit];}
    CGFloat scale=MIN(w.bounds.size.width/1376,w.bounds.size.height/1032);
    self.panel.transform=CGAffineTransformIdentity;self.panel.bounds=CGRectMake(0,0,1376,1032);
    self.panel.transform=CGAffineTransformMakeScale(scale,scale);
    self.panel.center=CGPointMake(w.bounds.size.width/2,w.bounds.size.height/2);
    self.curtain.frame=w.bounds;self.curtain.hidden=self.panel.hidden;
    unsigned phase=pcp_seek_phase();BOOL busy=phase>=1 && phase<=4,editing=_edit.editing!=0;
    self.apply.enabled=self.reset.enabled=s.ready&&!s.pending&&(!busy||s.prep);self.slider.enabled=(!busy||s.prep);
    self.noteMode.enabled=self.pitchMode.enabled=(!busy||s.prep);
    [self.noteMode setTitle:pcp_note_mode_get()?@"流速不变":@"同步变速" forState:UIControlStateNormal];
    [self.pitchMode setTitle:pcp_pitch_mode_get()?@"音高保持":@"音高随速" forState:UIControlStateNormal];
    self.noteSpeeds.text=hud.scroll_valid?[NSString stringWithFormat:@"CurrentNoteSpeed %.3f",hud.current_note_speed]:@"CurrentNoteSpeed --";
    self.noteSpeedValue.text=hud.scroll_valid?[NSString stringWithFormat:@"NoteSpeed %.2f",hud.note_speed]:@"NoteSpeed --";
    double desired=[self noteDesired];BOOL fixed=pcp_note_mode_get()!=0;
    self.noteControlValue.text=hud.scroll_valid?[NSString stringWithFormat:@"%.3f",fixed?hud.current_note_speed:desired]:@"--";
    if(!self.noteSlider.tracking)self.noteSlider.value=desired;
    self.noteSlider.enabled=!fixed&&!busy&&hud.scroll_valid;
    for(UIButton *button in self.noteSteps)button.enabled=self.noteSlider.enabled;
    self.noteControlValue.alpha=fixed?0.6:1;
    self.appliedValue.text=[NSString stringWithFormat:@"SPEED %.2fx",hud.percent/100.0];
    for(UIButton *button in self.rateSteps)button.enabled=self.slider.enabled;
    self.compLabel.text=[NSString stringWithFormat:@"补偿 %ums",pcp_pitch_comp_get()];
    PracticePoints p=pcp_points_snapshot();unsigned extent=MAX(1u,pcp_points_extent()),current=pcp_points_current();
    self.axis.points=p;self.axis.extent=extent;self.axis.current=current;self.axis.selected=self.freeEditing?5:self.selected;self.axis.userInteractionEnabled=!busy;self.axis.drafting=editing;self.axis.draftID=self.freeEditing?5:self.selected;self.axis.draftMs=(unsigned)self.milliseconds.text.longLongValue;self.axis.draftLimit=_edit.duration;[self.axis setNeedsDisplay];
    self.rangeLabel.text=[NSString stringWithFormat:@"Current - %@",PTTime(current)];
    BOOL has=self.selected>=0 && (p.mask&(1u<<self.selected));
    unsigned shownMs=editing?_edit.draft:(has?p.ms[self.selected]:current);
    self.editBox.hidden=NO;
    if(!editing)self.milliseconds.text=has?[NSString stringWithFormat:@"%u",p.ms[self.selected]]:@"--";
    for(UIButton *button in self.editButtons)button.enabled=(editing||has)&&!busy;
    for(UIButton *button in self.editCommitButtons)button.enabled=editing&&!busy;
    self.selection.text=self.freeEditing?[NSString stringWithFormat:@"选中 C · %@",PTTime(shownMs)]:has?[NSString stringWithFormat:@"选中 %@ · %@",PTName(self.selected),PTTime(shownMs)]:self.selected>=0?[NSString stringWithFormat:@"选中 %@ · 未设置",PTName(self.selected)]:@"选择点位开始编辑";
    [self.jump setTitle:@"跳转选中点" forState:UIControlStateNormal];
    self.jump.enabled=(has||editing)&&!busy;self.jump.alpha=self.jump.enabled?1:0.32;
    self.removeT.enabled=has&&!self.freeEditing&&!busy;
    NSInteger targetT=[self bookmarkTarget];self.addT.enabled=!busy&&targetT>=2;
    [self.addT setTitle:targetT>=2?[NSString stringWithFormat:@"设置 %@ 点",PTName(targetT)]:@"T 点已满" forState:UIControlStateNormal];
    for(UIButton *b in self.captureButtons)b.enabled=!busy;
    self.loop.enabled=!busy;
    NSString *loopTitle=pcp_ab_loop_get()?@"A-B 循环 开":@"A-B 循环 关";
    if(pcp_loop_suspended())loopTitle=@"A/B 循环-开 · 已暂挂";
    else if(pcp_ab_loop_get() && ((p.mask&3)!=3 || p.ms[1]<=p.ms[0]))loopTitle=@"A/B 循环-开 · 请设置 A < B";
    self.loop.selected=pcp_ab_loop_get();self.loop.accessibilityLabel=loopTitle;
    [self.hudMode setTitle:[self hudEnabled]?@"HUD显示":@"HUD隐藏" forState:UIControlStateNormal];
    self.previousPoint.enabled=self.nextPoint.enabled=self.freeSeek.enabled=!busy;
    self.freeSeek.selected=self.freeEditing;
    if(busy)self.status.text=phase==1?@"等待跳转条件…":phase==2?@"正在重试，等待新会话…":phase==3?(s.prep?@"预备段已暂停 · 可保存倍率，或继续准备完成跳转":@"等待预备段结束并定位…"):(s.visible?@"已暂停，恢复播放后继续确认":@"正在确认跳转位置…");
    else if(CFAbsoluteTimeGetCurrent()>self.messageUntil)
        self.status.text=phase==6?[NSString stringWithFormat:@"跳转未通过验证 (%u)，循环已停止",pcp_seek_error()]:phase==5?@"跳转验证通过":@"";
    [self setHudHidden:hudHidden||!self.panel.hidden];
    self.entry.hidden=YES;
}
@end
