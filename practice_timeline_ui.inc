/* Shared panel implementation. Compiled inside practice_rate_ui.m. */
#include "practice_design_skin.inc"
static void PTRegisterFonts(void) {
    for(NSString *name in @[@"Exo-Light.ttf",@"Exo-Regular.ttf",@"Exo-Medium.ttf",@"Exo-SemiBold.ttf",@"NotoSansCJKsc-Light.otf"]) {
        NSString *path=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:[@"Fonts/" stringByAppendingString:name]];
        if([NSFileManager.defaultManager fileExistsAtPath:path])
            CTFontManagerRegisterFontsForURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],kCTFontManagerScopeProcess,NULL);
    }
}
static UIFont *PTFont(CGFloat size,BOOL latin) {
    UIFont *font=[UIFont fontWithName:latin?@"Exo-Light":@"NotoSansCJKsc-Light" size:size];
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
        [[self skin:i<2?@"el_e415ec13":@"el_0aba565b" selected:selected] drawInRect:CGRectMake(x-size/2,y-size/2,size,size)];
        [self drawText:[NSString stringWithFormat:@"%@ %@",PTName(i),PTTime(self.drafting && self.draftID==(NSInteger)i?self.draftMs:self.points.ms[i])] x:x y:labelY color:selected?[UIColor colorWithRed:0.8 green:0.9 blue:1 alpha:1]:UIColor.whiteColor];
    }
    CGFloat x=[self xFor:self.drafting && self.draftID==5?self.draftMs:self.current];
    [[self skin:@"el_6680a60d" selected:NO] drawInRect:CGRectMake(x-48,y-48,96,96)];
    [self drawText:[NSString stringWithFormat:@"C %@",PTTime(self.current)] x:x y:94 color:[UIColor colorWithRed:0.5 green:0.95 blue:0.87 alpha:1]];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if(!self.drafting || !self.drag)return;
    CGFloat x=[touches.anyObject locationInView:self].x;
    double ratio=MAX(0,MIN(1,(x-20)/MAX(1,self.bounds.size.width-40)));
    uint64_t ms=(uint64_t)llround(ratio*self.extent/100.0)*100;
    ms=MIN(ms,(uint64_t)(self.draftLimit/100)*100);
    self.drag((unsigned)ms);
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if(self.drafting)return;
    CGPoint p=[touches.anyObject locationInView:self];NSInteger best=-1;CGFloat distance=32;
    // Separate label lanes keep coincident timestamps individually selectable.
    if(p.y<75){for(unsigned i=0;i<5;i++)if(self.points.mask&(1u<<i)){
        NSString *text=[NSString stringWithFormat:@"%@ %@",PTName(i),PTTime(self.points.ms[i])];
        CGFloat width=[text sizeWithAttributes:@{NSFontAttributeName:([UIFont fontWithName:@"Exo-Medium" size:18]?:PTFont(18,YES))}].width;
        CGFloat left=MAX(0,MIN(self.bounds.size.width-width,[self xFor:self.points.ms[i]]-width/2));
        if(CGRectContainsPoint(CGRectMake(left,i*15,width,15),p)){best=i;break;}
    }}else if(p.y<=110){for(unsigned i=0;i<5;i++)if(self.points.mask&(1u<<i)){
        CGFloat d=fabs(p.x-[self xFor:self.points.ms[i]]);if(d<distance || (d==distance && self.selected==(NSInteger)i)){distance=d;best=i;}
    }}
    if(best>=0 && self.choose)self.choose((NSUInteger)best);
}

@end

@interface PCPRatePanel : NSObject {
    PracticePoints _edit;
}
@property(nonatomic,strong) PCPRateOverlay *overlay;
@property(nonatomic,strong) UIView *panel,*editBox,*curtain,*keypad;
@property(nonatomic,strong) UIButton *entry,*apply,*reset,*noteMode,*pitchMode,*loop,*jump,*addT,*removeT,*close,*continueButton,*cancelJump;
@property(nonatomic,strong) NSMutableArray<UIButton *> *chips,*editButtons,*captureButtons;
@property(nonatomic,strong) UILabel *value,*status,*selection,*rangeLabel,*compLabel,*speedHUD,*accHUD,*noteSpeeds,*countdown,*keyboardTitle;
@property(nonatomic,strong) UILabel *noteSpeedValue,*appliedValue,*currentHUD,*durationHUD;
@property(nonatomic,strong) NSMutableArray<UIButton *> *rateSteps;
@property(nonatomic,strong) UILabel *noteControlValue;
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
@property(nonatomic) BOOL replaceDigits;
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
    v.titleLabel.font=PTFont(17,NO);v.titleLabel.numberOfLines=2;v.titleLabel.textAlignment=NSTextAlignmentCenter;
    [v addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];[self.panel addSubview:v];return v;
}
- (void)message:(NSString *)s {self.status.text=s;self.messageUntil=CFAbsoluteTimeGetCurrent()+3;}
#include "practice_design_panel.inc"
- (void)hideForTransition {
    self.autoRateQueued=NO;[self cancelEdit];self.panel.hidden=self.curtain.hidden=self.entry.hidden=YES;
    self.speedHUD.hidden=self.accHUD.hidden=self.currentHUD.hidden=self.durationHUD.hidden=self.countdown.hidden=YES;
    self.lastOpenSeq=pcp_rate_ui_state().open_seq;
}
- (void)continuePlaying {if(!pcp_native_continue())[self message:@"请返回暂停菜单后继续播放"];}
- (void)cancelWaiting {if(pcp_cancel_jump())[self message:@"已取消待执行跳转，可重新设置"];}
- (void)countdownTick {
    if(UIApplication.sharedApplication.applicationState!=UIApplicationStateActive || !self.panel.hidden || self.overlay.hidden) {self.countdown.hidden=YES;return;}
    BOOL paused=pcp_paused_fast() || pcp_rate_ui_state().visible;
    for(UILabel *l in @[self.speedHUD,self.accHUD,self.currentHUD,self.durationHUD])l.alpha=paused?0.35:1;
    unsigned playback=0;if(!self.currentHUD.hidden && pcp_playback_position(&playback))self.currentHUD.text=PTTime(playback);
    PracticeCountdownState c=pcp_countdown_state();self.countdown.hidden=!c.active||c.paused||paused;
    if(self.countdown.hidden)return;
    [self layoutDesignHUD];
    self.countdown.text=[NSString stringWithFormat:@"%u:%03u",c.remaining_ms/1000,c.remaining_ms%1000];
}
- (void)openDigits {self.keypad.hidden=NO;self.replaceDigits=YES;}
- (void)digit:(UIButton *)b {
    NSString *s=self.milliseconds.text?:@"0";
    if(b.tag==9)s=@"0";
    else if(b.tag==11)s=s.length>1?[s substringToIndex:s.length-1]:@"0";
    else {
        NSString *digit=b.tag==10?@"0":[NSString stringWithFormat:@"%ld",(long)b.tag+1];
        s=(self.replaceDigits || [s isEqualToString:@"0"])?digit:[s stringByAppendingString:digit];
        if(s.length>10){[self message:@"最多输入 10 位毫秒数"];return;}
    }
    self.replaceDigits=NO;self.milliseconds.text=s;
}
- (void)updateDraft {self.draft=MAX(50,MIN(200,self.draft));self.value.text=[NSString stringWithFormat:@"%.2fx",self.draft/100.0];self.slider.value=self.draft;}
- (void)slide:(UISlider *)s {self.draft=lroundf(s.value);[self updateDraft];[self queueAutoRate];}
- (void)step:(UIButton *)s {self.draft+=s.tag;[self updateDraft];[self queueAutoRate];}
- (void)toggle { [self cancelEdit];self.panel.hidden=!self.panel.hidden;if(!self.panel.hidden){self.draft=pcp_rate_ui_stored_percent();[self updateDraft];} }
- (void)applyRate {
    if(pcp_rate_ui_request((unsigned)self.draft,self.epoch)){pcp_rate_ui_save_percent((unsigned)self.draft);[self message:(pcp_rate_ui_state().prep?@"倍率已保存，将在开始播放时应用":@"倍率请求已提交")];}else [self message:@"暂停状态尚未就绪"];
}
- (void)resetRate {self.draft=100;[self updateDraft];[self applyRate];}
- (void)noteTap {pcp_note_mode_set(!pcp_note_mode_get());[self message:@"Note 模式将在重试或重新进歌后生效"];} 
- (void)pitchTap {pcp_pitch_mode_set(!pcp_pitch_mode_get());}
- (void)comp:(UIButton *)b {if(pcp_seek_phase()>=1 && pcp_seek_phase()<=4)return;NSInteger v=(NSInteger)pcp_pitch_comp_get()+b.tag;pcp_pitch_comp_set((unsigned)MAX(0,MIN(80,v)));}
- (void)capture:(UIButton *)b {[self cancelEdit];if(pcp_point_capture((unsigned)b.tag)){self.selected=b.tag;[self message:@"已用当前播放位置设点"];}else [self message:@"当前无法设点"];} 
- (void)addBookmark {
    if(self.selected<2 || self.selected>=5){[self message:@"请先选择 T1 / T2 / T3"];return;}
    [self cancelEdit];if(pcp_point_capture((unsigned)self.selected))[self message:[NSString stringWithFormat:@"已设置 %@",PTName(self.selected)]];
    else [self message:@"当前无法设点"];
}
- (void)deletePoint {if(self.selected>=2 && pcp_point_delete((unsigned)self.selected)){[self cancelEdit];self.selected=-1;[self message:@"已删除书签，其他编号保留"];}}
- (void)choose:(NSUInteger)i {
    if(i>=5 || (pcp_seek_phase()>=1 && pcp_seek_phase()<=4))return;
    [self cancelEdit];self.selected=(NSInteger)i;
    PracticePoints p=pcp_points_snapshot();
    if(!(p.mask&(1u<<i))){[self message:@"空点位，先使用设置按钮记录当前时间"];return;}
    _edit=p;_edit.selected=(int)i;_edit.editing=0;_edit.duration=p.duration?p.duration-1:pcp_points_extent();
    if(pp_select(&_edit,(unsigned)i)!=2)return;
    self.milliseconds.text=[NSString stringWithFormat:@"%u",_edit.draft];
    self.editBox.hidden=NO;self.keypad.hidden=YES;self.replaceDigits=YES;
}
- (void)cyclePoint:(UIButton *)button {
    NSInteger next=self.selected<0?(button.tag<0?4:0):(self.selected+button.tag+5)%5;
    [self choose:(NSUInteger)next];
}
- (void)freeSeekTap {
    if(pcp_seek_phase()>=1 && pcp_seek_phase()<=4)return;
    [self cancelEdit];self.freeEditing=YES;
    PracticePoints p=pcp_points_snapshot();pp_init(&_edit,p.duration?p.duration-1:pcp_points_extent());
    _edit.editing=1;_edit.draft=MIN(pcp_points_current(),_edit.duration);
    self.milliseconds.text=[NSString stringWithFormat:@"%u",_edit.draft];self.editBox.hidden=NO;self.replaceDigits=YES;
}
- (void)dragTime:(unsigned)ms {
    if(self.editBox.hidden || !pp_input(&_edit,ms))return;
    self.milliseconds.text=[NSString stringWithFormat:@"%u",ms];self.axis.draftMs=ms;[self.axis setNeedsDisplay];
}
- (void)stopHold {[self.holdTimer invalidate];self.holdTimer=nil;self.heldButton=nil;}
- (void)repeatTime {
    if(self.editBox.hidden || !self.heldButton.enabled || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive){[self stopHold];return;}
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
- (void)noteUnavailable {[self message:@"局内即时流速重建尚未验证"];}
- (void)chip:(UIButton *)b {[self choose:(NSUInteger)b.tag];}
- (BOOL)readInput {
    NSString *s=self.milliseconds.text;
    if(!s.length || [s rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789"].invertedSet].location!=NSNotFound || s.length>10)return NO;
    unsigned long long value=strtoull(s.UTF8String,NULL,10);
    return value<=INT32_MAX && pp_input(&_edit,(uint32_t)value);
}
- (void)timeStep:(UIButton *)b {
    if(![self readInput]){[self message:@"请输入范围内的整数毫秒数"];return;}
    pp_adjust(&_edit,(int)b.tag);self.milliseconds.text=[NSString stringWithFormat:@"%u",_edit.draft];
}
- (void)confirmEdit {
    if(self.freeEditing){
        if(![self readInput] || !pcp_jump_time(_edit.draft)){[self message:@"无法提交定位，请确认时间范围与暂停状态"];return;}
        [self hideForTransition];return;
    }
    if(![self readInput] || !pcp_point_update((unsigned)self.selected,_edit.draft)){[self message:@"时间超出范围或会话已变化，尚未保存"];return;}
    [self cancelEdit];[self message:@"时间已更新"];
}
- (void)cancelEdit {[self stopHold];self.freeEditing=NO;self.axis.drafting=NO;self.keypad.hidden=YES;self.keyboardTitle.hidden=YES;_edit.editing=0;self.editBox.hidden=YES;self.jump.hidden=self.selection.hidden=NO;}
- (void)jumpTap {
    if(self.selected>=0 && pcp_point_jump((unsigned)self.selected)){[self hideForTransition];}
    else [self message:@"无法提交跳转，请保持暂停并选中点位"];
}
- (void)loopTap {pcp_ab_loop_set(pcp_loop_suspended()?1:!pcp_ab_loop_get());}
- (UIWindow *)gameWindow {
    for(UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if(scene.activationState==UISceneActivationStateForegroundActive && [scene isKindOfClass:UIWindowScene.class])
            for(UIWindow *w in ((UIWindowScene *)scene).windows)if(w.isKeyWindow && !w.hidden && w.windowLevel==UIWindowLevelNormal)return w;
    for(UIWindow *w in UIApplication.sharedApplication.windows)if(w.isKeyWindow && !w.hidden && w.windowLevel==UIWindowLevelNormal)return w;
    return nil;
}
- (void)tick {
    [self flushAutoRate];PracticeRateUIState s=pcp_rate_ui_state();UIWindow *w=[self gameWindow];
    if(!w || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive || s.transitioning){[self hideForTransition];self.overlay.hidden=YES;return;}
    if(self.overlay.superview!=w){[self.overlay removeFromSuperview];[w addSubview:self.overlay];}
    self.overlay.frame=w.bounds;[w bringSubviewToFront:self.overlay];self.overlay.hidden=NO;
    PracticeHUDState hud=pcp_hud_state();
    self.speedHUD.hidden=self.accHUD.hidden=self.currentHUD.hidden=self.durationHUD.hidden=!hud.visible || !self.panel.hidden;
    [self layoutDesignHUD];
    self.speedHUD.text=[NSString stringWithFormat:@"SPEED %.2fx",hud.percent/100.0];
    self.accHUD.text=hud.score_valid?[NSString stringWithFormat:@"ACC %.2f%%",hud.accuracy]:@"ACC --";
    self.currentHUD.text=PTTime(pcp_points_current());
    PracticePoints hudPoints=pcp_points_snapshot();self.durationHUD.text=hudPoints.duration?PTTime(hudPoints.duration):@"--:--.---";
    for(UILabel *l in @[self.speedHUD,self.accHUD,self.currentHUD,self.durationHUD])l.alpha=(s.visible||pcp_paused_fast())?0.35:1;
    self.entry.hidden=YES; /* Native pause row owns the entry. */
    self.entry.frame=CGRectMake(w.bounds.size.width-w.safeAreaInsets.right-162,w.safeAreaInsets.top+12,150,40);
    [self.entry setTitle:[NSString stringWithFormat:@"倍率 %.2fx",s.applied/100.0] forState:UIControlStateNormal];
    if(!s.visible){self.panel.hidden=YES;self.curtain.hidden=YES;self.speedHUD.hidden=self.accHUD.hidden=self.currentHUD.hidden=self.durationHUD.hidden=!hud.visible;[self cancelEdit];return;}
    if(self.epoch!=s.epoch){self.panel.hidden=YES;[self cancelEdit];}self.epoch=s.epoch;
    if(s.open_seq!=self.lastOpenSeq){self.lastOpenSeq=s.open_seq;self.panel.hidden=NO;self.draft=pcp_rate_ui_stored_percent();[self updateDraft];}
    if(self.pointEpoch!=pcp_points_epoch()){self.pointEpoch=pcp_points_epoch();self.selected=-1;[self cancelEdit];}
    CGFloat scale=MIN(w.bounds.size.width/1376,w.bounds.size.height/1032);
    self.panel.transform=CGAffineTransformIdentity;self.panel.bounds=CGRectMake(0,0,1376,1032);
    self.panel.transform=CGAffineTransformMakeScale(scale,scale);
    self.panel.center=CGPointMake(w.bounds.size.width/2,w.bounds.size.height/2);
    self.curtain.frame=w.bounds;self.curtain.hidden=self.panel.hidden;
    unsigned phase=pcp_seek_phase();BOOL busy=phase>=1 && phase<=4,editing=!self.editBox.hidden;
    self.apply.enabled=self.reset.enabled=s.ready&&!s.pending&&(!busy||s.prep)&&!editing;self.slider.enabled=(!busy||s.prep)&&!editing;
    self.noteMode.enabled=self.pitchMode.enabled=(!busy||s.prep)&&!editing;
    [self.noteMode setTitle:pcp_note_mode_get()?@"流速不变":@"同步变速" forState:UIControlStateNormal];
    [self.pitchMode setTitle:pcp_pitch_mode_get()?(hud.pitch_attached || hud.percent==100?@"音高 保持":@"音高 保持 · 待挂接"):@"音高 随速" forState:UIControlStateNormal];
    self.noteSpeeds.text=hud.scroll_valid?[NSString stringWithFormat:@"CurrentNoteSpeed %.3f",hud.current_note_speed]:@"CurrentNoteSpeed --";
    self.noteSpeedValue.text=hud.scroll_valid?[NSString stringWithFormat:@"NoteSpeed %.2f",hud.note_speed]:@"NoteSpeed --";
    self.noteControlValue.text=hud.scroll_valid?[NSString stringWithFormat:@"%.3f",hud.current_note_speed]:@"--";
    self.appliedValue.text=[NSString stringWithFormat:@"SPEED %.2fx",hud.percent/100.0];
    for(UIButton *button in self.rateSteps)button.enabled=self.slider.enabled;
    self.compLabel.text=[NSString stringWithFormat:@"补偿 %ums",pcp_pitch_comp_get()];
    PracticePoints p=pcp_points_snapshot();unsigned extent=MAX(1u,pcp_points_extent()),current=pcp_points_current();
    self.axis.points=p;self.axis.extent=extent;self.axis.current=current;self.axis.selected=self.selected;self.axis.userInteractionEnabled=!busy;self.axis.drafting=editing;self.axis.draftID=self.freeEditing?5:self.selected;self.axis.draftMs=(unsigned)self.milliseconds.text.longLongValue;self.axis.draftLimit=_edit.duration;[self.axis setNeedsDisplay];
    self.rangeLabel.text=[NSString stringWithFormat:@"Current - %@",PTTime(current)];
    for(unsigned i=0;i<5;i++) {
        PTTextureButton *b=(PTTextureButton *)self.chips[i];[b setTitle:PTName(i) forState:UIControlStateNormal];
        b.enabled=!busy&&!editing;b.unset=!(p.mask&(1u<<i));b.selected=self.selected==(NSInteger)i;
        b.accessibilityLabel=[NSString stringWithFormat:@"%@ %@",PTName(i),(p.mask&(1u<<i))?PTTime(p.ms[i]):@"未设置"];
    }
    BOOL has=self.selected>=0 && (p.mask&(1u<<self.selected));
    self.selection.text=self.freeEditing?@"自由定位 C · 确认后跳转":has?[NSString stringWithFormat:@"%@  %@",PTName(self.selected),PTTime(p.ms[self.selected])]:@"点击时间轴或左右箭头选点";
    [self.jump setTitle:has?[NSString stringWithFormat:@"跳转到 %@ 点",PTName(self.selected)]:@"选中点位后跳转" forState:UIControlStateNormal];
    self.jump.enabled=has&&!busy&&!editing;self.jump.alpha=self.jump.enabled?1:0.45;
    self.removeT.enabled=has&&self.selected>=2&&!busy&&editing;
    self.addT.enabled=!busy&&self.selected>=2&&self.selected<5;
    [self.addT setTitle:self.selected>=2&&self.selected<5?[NSString stringWithFormat:@"设置 %@ 点",PTName(self.selected)]:@"设置 Tn 点" forState:UIControlStateNormal];
    for(UIButton *b in self.captureButtons)b.enabled=!busy;
    self.loop.enabled=!busy&&!editing;
    self.cancelJump.hidden=!(phase==3 && s.visible && !editing);
    self.continueButton.hidden=!(s.visible && !editing && (s.prep || busy));
    [self.continueButton setTitle:s.prep?@"继续准备":@"继续播放" forState:UIControlStateNormal];
    NSString *loopTitle=pcp_ab_loop_get()?@"A-B 循环 开":@"A-B 循环 关";
    if(pcp_loop_suspended())loopTitle=@"A/B 循环-开 · 已暂挂";
    else if(pcp_ab_loop_get() && ((p.mask&3)!=3 || p.ms[1]<=p.ms[0]))loopTitle=@"A/B 循环-开 · 请设置 A < B";
    self.loop.selected=pcp_ab_loop_get();self.loop.accessibilityLabel=loopTitle;
    self.previousPoint.enabled=self.nextPoint.enabled=self.freeSeek.enabled=!busy;
    self.freeSeek.selected=self.freeEditing;
    if(busy)self.status.text=phase==1?@"等待跳转条件…":phase==2?@"正在重试，等待新会话…":phase==3?(s.prep?@"预备段已暂停 · 可保存倍率，或继续准备完成跳转":@"等待预备段结束并定位…"):(s.visible?@"已暂停，恢复播放后继续确认":@"正在确认跳转位置…");
    else if(CFAbsoluteTimeGetCurrent()>self.messageUntil)
        self.status.text=phase==6?[NSString stringWithFormat:@"跳转未通过验证 (%u)，循环已停止",pcp_seek_error()]:phase==5?@"已跳转 · 可继续练习":@"单击选点编辑 · 金色按钮自由定位";
    self.speedHUD.hidden=self.accHUD.hidden=self.currentHUD.hidden=self.durationHUD.hidden=!hud.visible || !self.panel.hidden;
    self.entry.hidden=YES;
}
@end
