/* Shared panel implementation. Compiled inside practice_rate_ui.m. */
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
- (void)drawRect:(CGRect)rect {
    CGContextRef c=UIGraphicsGetCurrentContext();
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGFloat colors[]={0.12,0.08,0.28,0.92, 0.24,0.16,0.43,0.88, 0.10,0.07,0.23,0.94};
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
@end
@implementation PTTimeline
- (CGFloat)xFor:(unsigned)ms {return 28+(self.bounds.size.width-56)*MIN(1.0,ms/(double)MAX(1u,self.extent));}
- (void)drawText:(NSString *)s x:(CGFloat)x y:(CGFloat)y color:(UIColor *)c {
    NSDictionary *a=@{NSFontAttributeName:[UIFont monospacedDigitSystemFontOfSize:14 weight:UIFontWeightMedium],NSForegroundColorAttributeName:c};
    CGFloat w=[s sizeWithAttributes:a].width;
    [s drawAtPoint:CGPointMake(MAX(0,MIN(self.bounds.size.width-w,x-w/2)),y) withAttributes:a];
}
- (void)drawRect:(CGRect)rect {
    CGContextRef c=UIGraphicsGetCurrentContext();CGFloat w=self.bounds.size.width,y=101;
    if((self.points.mask&3)==3 && self.points.ms[1]>self.points.ms[0]) {
        [[UIColor colorWithRed:0.65 green:0.5 blue:1 alpha:0.22] setFill];
        UIRectFill(CGRectMake([self xFor:self.points.ms[0]],y-16,[self xFor:self.points.ms[1]]-[self xFor:self.points.ms[0]],32));
    }
    CGContextSetStrokeColorWithColor(c,[UIColor colorWithWhite:1 alpha:0.7].CGColor);
    CGContextSetLineWidth(c,1);CGContextMoveToPoint(c,28,y);CGContextAddLineToPoint(c,w-28,y);CGContextStrokePath(c);
    for(int i=0;i<=4;i++) {
        CGFloat x=28+(w-56)*i/4;CGContextMoveToPoint(c,x,y-4);CGContextAddLineToPoint(c,x,y+4);CGContextStrokePath(c);
        [self drawText:PTTime((unsigned)((uint64_t)self.extent*i/4)) x:x y:118 color:[UIColor colorWithWhite:1 alpha:0.55]];
    }
    for(unsigned i=0;i<5;i++) if(self.points.mask&(1u<<i)) {
        CGFloat x=[self xFor:self.points.ms[i]],labelY=4+i*18;
        UIColor *color=self.selected==(NSInteger)i?[UIColor colorWithRed:0.7 green:0.87 blue:1 alpha:1]:UIColor.whiteColor;
        CGContextSetStrokeColorWithColor(c,color.CGColor);CGContextMoveToPoint(c,x,y);CGContextAddLineToPoint(c,x,labelY+16);CGContextStrokePath(c);
        UIBezierPath *d=[UIBezierPath bezierPath];[d moveToPoint:CGPointMake(x,y-7)];[d addLineToPoint:CGPointMake(x+7,y)];[d addLineToPoint:CGPointMake(x,y+7)];[d addLineToPoint:CGPointMake(x-7,y)];[d closePath];[color setFill];[d fill];
        [self drawText:[NSString stringWithFormat:@"%@ %@",PTName(i),PTTime(self.points.ms[i])] x:x y:labelY color:color];
    }
    CGFloat currentX=[self xFor:self.current];
    CGContextSetStrokeColorWithColor(c,[UIColor colorWithRed:0.5 green:0.95 blue:0.87 alpha:1].CGColor);
    CGContextSetLineWidth(c,2);CGContextMoveToPoint(c,currentX,90);CGContextAddLineToPoint(c,currentX,112);CGContextStrokePath(c);
    [self drawText:@"C" x:currentX y:94 color:[UIColor colorWithRed:0.5 green:0.95 blue:0.87 alpha:1]];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    CGPoint p=[touches.anyObject locationInView:self];NSInteger best=-1;CGFloat distance=26;
    if(p.y<96) {
        for(unsigned i=0;i<5;i++) if(self.points.mask&(1u<<i)) {
            NSString *label=[NSString stringWithFormat:@"%@ %@",PTName(i),PTTime(self.points.ms[i])];
            CGFloat width=[label sizeWithAttributes:@{NSFontAttributeName:[UIFont monospacedDigitSystemFontOfSize:14 weight:UIFontWeightMedium]}].width;
            CGFloat left=MAX(0,MIN(self.bounds.size.width-width,[self xFor:self.points.ms[i]]-width/2));
            if(CGRectContainsPoint(CGRectMake(left,4+i*18,width,18),p)){best=i;break;}
        }
    } else for(unsigned i=0;i<5;i++)if(self.points.mask&(1u<<i)) {
        CGFloat d=fabs(p.x-[self xFor:self.points.ms[i]]);
        if(d<distance || (d==distance && self.selected==(NSInteger)i)) {distance=d;best=i;}
    }
    if(best>=0 && self.choose)self.choose((NSUInteger)best);
}
@end

@interface PCPRatePanel : NSObject {
    PracticePoints _edit;
}
@property(nonatomic,strong) PCPRateOverlay *overlay;
@property(nonatomic,strong) UIView *panel,*editBox,*curtain,*keypad;
@property(nonatomic,strong) UIButton *entry,*apply,*reset,*noteMode,*pitchMode,*loop,*jump,*addT,*removeT,*close;
@property(nonatomic,strong) NSMutableArray<UIButton *> *chips,*editButtons,*captureButtons;
@property(nonatomic,strong) UILabel *value,*status,*selection,*rangeLabel,*compLabel,*speedHUD,*accHUD,*noteSpeeds;
@property(nonatomic,strong) UISlider *slider;
@property(nonatomic,strong) UILabel *milliseconds;
@property(nonatomic,strong) PTTimeline *axis;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic) NSInteger draft,selected;
@property(nonatomic) uint64_t epoch,lastOpenSeq,pointEpoch;
@property(nonatomic) NSTimeInterval messageUntil;
@property(nonatomic) BOOL replaceDigits;
@end
@implementation PCPRatePanel
- (UILabel *)label:(CGFloat)size frame:(CGRect)frame text:(NSString *)text {
    UILabel *v=[[UILabel alloc] initWithFrame:frame];v.textColor=UIColor.whiteColor;
    v.font=[UIFont monospacedDigitSystemFontOfSize:size weight:UIFontWeightMedium];v.text=text;v.textAlignment=NSTextAlignmentCenter;
    [self.panel addSubview:v];return v;
}
- (UIButton *)button:(NSString *)title frame:(CGRect)frame action:(SEL)action {
    PTHexButton *v=[[PTHexButton alloc] initWithFrame:frame];v.backgroundColor=UIColor.clearColor;
    [v setTitle:title forState:UIControlStateNormal];[v setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    v.titleLabel.font=[UIFont systemFontOfSize:17 weight:UIFontWeightMedium];v.titleLabel.numberOfLines=2;v.titleLabel.textAlignment=NSTextAlignmentCenter;
    [v addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];[self.panel addSubview:v];return v;
}
- (void)message:(NSString *)s {self.status.text=s;self.messageUntil=CFAbsoluteTimeGetCurrent()+3;}
- (void)build {
    self.selected=-1;self.overlay=[PCPRateOverlay new];
    self.curtain=[PTBackdrop new];self.curtain.hidden=YES;[self.overlay addSubview:self.curtain];
    self.panel=[UIView new];self.panel.hidden=YES;self.panel.backgroundColor=UIColor.clearColor;
    [self.overlay addSubview:self.panel];
    for(NSNumber *n in @[@66,@211,@388,@508,@624]) {
        UIView *line=[[UIView alloc] initWithFrame:CGRectMake(20,n.doubleValue,960,0.7)];
        line.backgroundColor=[UIColor colorWithWhite:1 alpha:0.35];[self.panel addSubview:line];
    }
    [self label:26 frame:CGRectMake(32,18,200,40) text:@"练习设置"];
    self.close=[self button:@"关闭 ×" frame:CGRectMake(850,18,118,40) action:@selector(toggle)];
    self.value=[self label:30 frame:CGRectMake(32,78,150,42) text:@"1.00x"];
    self.slider=[[UISlider alloc] initWithFrame:CGRectMake(206,78,385,42)];
    self.slider.minimumValue=50;self.slider.maximumValue=200;self.slider.continuous=YES;
    self.slider.tintColor=[UIColor colorWithRed:0.75 green:0.64 blue:1 alpha:1];
    [self.slider addTarget:self action:@selector(slide:) forControlEvents:UIControlEventValueChanged];[self.panel addSubview:self.slider];
    self.apply=[self button:@"应用倍率" frame:CGRectMake(620,76,166,44) action:@selector(applyRate)];
    self.reset=[self button:@"恢复 1.00x" frame:CGRectMake(802,76,166,44) action:@selector(resetRate)];
    int deltas[]={-5,-1,1,5};NSArray *names=@[@"− 0.05",@"− 0.01",@"+ 0.01",@"+ 0.05"];
    for(int i=0;i<4;i++){UIButton *b=[self button:names[i] frame:CGRectMake(32+i*99,134,91,40) action:@selector(step:)];b.tag=deltas[i];}
    self.noteMode=[self button:@"Note 同步变速" frame:CGRectMake(446,134,254,40) action:@selector(noteTap)];
    self.pitchMode=[self button:@"音高 随速" frame:CGRectMake(716,134,252,40) action:@selector(pitchTap)];
    self.noteSpeeds=[self label:13 frame:CGRectMake(32,179,660,27) text:@"CurrentNoteSpeed --   ·   NoteSpeed --"];
    UIButton *minus=[self button:@"−5ms" frame:CGRectMake(706,179,74,28) action:@selector(comp:)];minus.tag=-5;
    self.compLabel=[self label:12 frame:CGRectMake(782,179,110,28) text:@"补偿 0ms"];
    UIButton *plus=[self button:@"+5ms" frame:CGRectMake(894,179,74,28) action:@selector(comp:)];plus.tag=5;
    self.rangeLabel=[self label:15 frame:CGRectMake(32,214,936,28) text:@"读取时间范围…"];
    self.axis=[[PTTimeline alloc] initWithFrame:CGRectMake(32,244,936,145)];self.axis.backgroundColor=UIColor.clearColor;
    __weak PCPRatePanel *weakSelf=self;self.axis.choose=^(NSUInteger i){[weakSelf choose:i];};[self.panel addSubview:self.axis];
    self.chips=[NSMutableArray array];
    for(int i=0;i<5;i++){UIButton *b=[self button:PTName(i) frame:CGRectMake(32+i*190,395,176,48) action:@selector(chip:)];b.tag=i;[self.chips addObject:b];}
    UIButton *a=[self button:@"设置 A" frame:CGRectMake(32,458,172,42) action:@selector(capture:)];a.tag=0;
    UIButton *b=[self button:@"设置 B" frame:CGRectMake(220,458,172,42) action:@selector(capture:)];b.tag=1;
    self.captureButtons=[NSMutableArray arrayWithObjects:a,b,nil];
    self.addT=[self button:@"新增 T" frame:CGRectMake(408,458,172,42) action:@selector(addBookmark)];
    self.loop=[self button:@"A/B 循环-关" frame:CGRectMake(612,458,356,42) action:@selector(loopTap)];
    self.selection=[self label:18 frame:CGRectMake(32,518,936,32) text:@"点击 A / B / T 选中，再次点击编辑"];
    self.jump=[self button:@"选中点位后跳转" frame:CGRectMake(250,562,500,48) action:@selector(jumpTap)];
    self.editBox=[[UIView alloc] initWithFrame:CGRectMake(32,512,936,108)];self.editBox.hidden=YES;[self.panel addSubview:self.editBox];
    self.milliseconds=[[UILabel alloc] initWithFrame:CGRectMake(0,0,230,44)];
    self.milliseconds.textColor=UIColor.whiteColor;self.milliseconds.backgroundColor=[UIColor colorWithWhite:1 alpha:0.1];
    self.milliseconds.font=[UIFont monospacedDigitSystemFontOfSize:24 weight:UIFontWeightMedium];
    self.milliseconds.textAlignment=NSTextAlignmentCenter;self.milliseconds.userInteractionEnabled=YES;
    [self.milliseconds addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(openDigits)]];
    [self.editBox addSubview:self.milliseconds];
    UILabel *unit=[[UILabel alloc] initWithFrame:CGRectMake(235,0,60,44)];unit.text=@"ms";unit.textColor=UIColor.whiteColor;[self.editBox addSubview:unit];
    int steps[]={-1000,-100,100,1000};self.editButtons=[NSMutableArray array];
    for(int i=0;i<4;i++) {
        UIButton *v=[self button:[NSString stringWithFormat:@"%+d",steps[i]] frame:CGRectMake(300+i*160,0,146,44) action:@selector(timeStep:)];
        v.tag=steps[i];[v removeFromSuperview];[self.editBox addSubview:v];[self.editButtons addObject:v];
    }
    UIButton *confirm=[self button:@"确认时间" frame:CGRectMake(300,58,220,44) action:@selector(confirmEdit)];
    [confirm removeFromSuperview];[self.editBox addSubview:confirm];
    UIButton *cancel=[self button:@"取消" frame:CGRectMake(536,58,220,44) action:@selector(cancelEdit)];
    [cancel removeFromSuperview];[self.editBox addSubview:cancel];
    self.removeT=[self button:@"删除 T" frame:CGRectMake(0,58,220,44) action:@selector(deletePoint)];
    [self.removeT removeFromSuperview];[self.editBox addSubview:self.removeT];
    self.keypad=[[PTBackdrop alloc] initWithFrame:CGRectMake(256,244,488,256)];self.keypad.hidden=YES;
    [self.panel addSubview:self.keypad];
    NSArray *keys=@[@"1",@"2",@"3",@"4",@"5",@"6",@"7",@"8",@"9",@"清空",@"0",@"退格"];
    for(int i=0;i<12;i++) {
        UIButton *key=[self button:keys[i] frame:CGRectMake(12+(i%3)*158,8+(i/3)*61,148,53) action:@selector(digit:)];
        key.tag=i;[key removeFromSuperview];[self.keypad addSubview:key];
    }
    self.status=[self label:14 frame:CGRectMake(32,632,936,28) text:@"先选择点位，再跳转；跳转会重试并开始练习"];
    self.entry=[self button:@"倍率 1.00x" frame:CGRectZero action:@selector(toggle)];[self.entry removeFromSuperview];[self.overlay addSubview:self.entry];
    self.speedHUD=[self label:20 frame:CGRectZero text:@"SPEED 1.00x"];
    self.accHUD=[self label:20 frame:CGRectZero text:@"ACC --"];
    for(UILabel *l in @[self.speedHUD,self.accHUD]) {
        [l removeFromSuperview];[self.overlay addSubview:l];l.textAlignment=NSTextAlignmentRight;
        l.layer.shadowColor=UIColor.blackColor.CGColor;l.layer.shadowOpacity=0.65;l.layer.shadowRadius=3;l.layer.shadowOffset=CGSizeMake(0,1);
    }
    self.draft=pcp_rate_ui_stored_percent();[self updateDraft];
}
- (void)openDigits {self.keypad.hidden=!self.keypad.hidden;self.replaceDigits=YES;}
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
- (void)slide:(UISlider *)s {self.draft=lroundf(s.value);[self updateDraft];}
- (void)step:(UIButton *)s {self.draft+=s.tag;[self updateDraft];}
- (void)toggle { [self cancelEdit];self.panel.hidden=!self.panel.hidden;if(!self.panel.hidden){self.draft=pcp_rate_ui_stored_percent();[self updateDraft];} }
- (void)applyRate {
    if(pcp_rate_ui_request((unsigned)self.draft,self.epoch)){pcp_rate_ui_save_percent((unsigned)self.draft);[self message:@"倍率请求已提交"];}else [self message:@"暂停状态尚未就绪"];
}
- (void)resetRate {self.draft=100;[self updateDraft];[self applyRate];}
- (void)noteTap {pcp_note_mode_set(!pcp_note_mode_get());[self message:@"Note 模式将在重试或重新进歌后生效"];} 
- (void)pitchTap {pcp_pitch_mode_set(!pcp_pitch_mode_get());}
- (void)comp:(UIButton *)b {if(pcp_seek_phase()>=1 && pcp_seek_phase()<=4)return;NSInteger v=(NSInteger)pcp_pitch_comp_get()+b.tag;pcp_pitch_comp_set((unsigned)MAX(0,MIN(80,v)));}
- (void)capture:(UIButton *)b {if(pcp_point_capture((unsigned)b.tag)){self.selected=b.tag;[self message:@"已用当前播放位置设点"];}else [self message:@"当前无法设点"];} 
- (void)addBookmark {
    PracticePoints p=pcp_points_snapshot();
    for(unsigned i=2;i<5;i++)if(!(p.mask&(1u<<i))){if(pcp_point_capture(i)){self.selected=i;[self message:[NSString stringWithFormat:@"已添加 %@",PTName(i)]];}return;}
    [self message:@"三个 T 点已满，可编辑或删除已有点"];
}
- (void)deletePoint {if(self.selected>=2 && pcp_point_delete((unsigned)self.selected)){[self cancelEdit];self.selected=-1;[self message:@"已删除书签，其他编号保留"];}}
- (void)choose:(NSUInteger)i {
    if(!self.editBox.hidden || (pcp_seek_phase()>=1 && pcp_seek_phase()<=4))return;
    PracticePoints p=pcp_points_snapshot();if(!(p.mask&(1u<<i)))return;
    if(self.selected!=(NSInteger)i){self.selected=i;return;}
    _edit=p;_edit.selected=(int)i;_edit.editing=0;
    _edit.duration=p.duration?p.duration-1:pcp_points_extent();
    if(pp_select(&_edit,(unsigned)i)!=2)return;
    self.milliseconds.text=[NSString stringWithFormat:@"%u",_edit.draft];
    self.editBox.hidden=NO;self.jump.hidden=self.selection.hidden=YES;self.removeT.hidden=i<2;self.keypad.hidden=YES;
}
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
    if(![self readInput] || !pcp_point_update((unsigned)self.selected,_edit.draft)){[self message:@"时间超出范围或会话已变化，尚未保存"];return;}
    [self cancelEdit];[self message:@"时间已更新"];
}
- (void)cancelEdit {self.keypad.hidden=YES;_edit.editing=0;self.editBox.hidden=YES;self.jump.hidden=self.selection.hidden=NO;}
- (void)jumpTap {
    if(self.selected>=0 && pcp_point_jump((unsigned)self.selected)){[self message:@"正在重试并定位…"];[self.panel endEditing:YES];}
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
    PracticeRateUIState s=pcp_rate_ui_state();UIWindow *w=[self gameWindow];
    if(!w || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive){[self cancelEdit];self.overlay.hidden=YES;self.curtain.hidden=YES;return;}
    if(self.overlay.superview!=w){[self.overlay removeFromSuperview];[w addSubview:self.overlay];}
    self.overlay.frame=w.bounds;[w bringSubviewToFront:self.overlay];self.overlay.hidden=NO;
    PracticeHUDState hud=pcp_hud_state();
    self.speedHUD.hidden=self.accHUD.hidden=!hud.visible || !self.panel.hidden;
    CGFloat hudW=MIN(240,w.bounds.size.width*0.23),hudX=w.bounds.size.width-w.safeAreaInsets.right-hudW-20;
    self.speedHUD.frame=CGRectMake(hudX,w.bounds.size.height*0.31,hudW,34);
    self.accHUD.frame=CGRectMake(hudX,w.bounds.size.height*0.58,hudW,34);
    self.speedHUD.text=[NSString stringWithFormat:@"SPEED %.2fx",hud.percent/100.0];
    self.accHUD.text=hud.score_valid?[NSString stringWithFormat:@"ACC %.2f%%",hud.accuracy]:@"ACC --";
    self.entry.hidden=!s.visible;
    self.entry.frame=CGRectMake(w.bounds.size.width-w.safeAreaInsets.right-162,w.safeAreaInsets.top+12,150,40);
    [self.entry setTitle:[NSString stringWithFormat:@"倍率 %.2fx",s.applied/100.0] forState:UIControlStateNormal];
    if(!s.visible){self.panel.hidden=YES;self.curtain.hidden=YES;self.speedHUD.hidden=self.accHUD.hidden=!hud.visible;[self cancelEdit];return;}
    if(self.epoch!=s.epoch){self.panel.hidden=YES;[self cancelEdit];}self.epoch=s.epoch;
    if(s.open_seq!=self.lastOpenSeq){self.lastOpenSeq=s.open_seq;self.panel.hidden=NO;self.draft=pcp_rate_ui_stored_percent();[self updateDraft];}
    if(self.pointEpoch!=pcp_points_epoch()){self.pointEpoch=pcp_points_epoch();self.selected=-1;[self cancelEdit];}
    CGFloat h=w.bounds.size.height-w.safeAreaInsets.top-w.safeAreaInsets.bottom-24;
    CGFloat width=w.bounds.size.width-w.safeAreaInsets.left-w.safeAreaInsets.right-24;
    CGFloat scale=MAX(0.3,MIN(1,MIN(width/1000,h/680)));
    self.panel.transform=CGAffineTransformIdentity;self.panel.bounds=CGRectMake(0,0,1000,680);
    self.panel.transform=CGAffineTransformMakeScale(scale,scale);
    self.panel.center=CGPointMake(w.bounds.size.width/2,w.safeAreaInsets.top+12+h/2);
    self.curtain.frame=w.bounds;self.curtain.hidden=self.panel.hidden;
    unsigned phase=pcp_seek_phase();BOOL busy=phase>=1 && phase<=4,editing=!self.editBox.hidden;
    self.apply.enabled=self.reset.enabled=s.ready&&!s.pending&&!busy&&!editing;self.slider.enabled=!s.pending&&!busy&&!editing;
    self.noteMode.enabled=self.pitchMode.enabled=!busy&&!editing;
    [self.noteMode setTitle:pcp_note_mode_get()?@"Note 流速不变":@"Note 同步变速" forState:UIControlStateNormal];
    [self.pitchMode setTitle:pcp_pitch_mode_get()?(hud.pitch_attached || hud.percent==100?@"音高 保持":@"音高 保持 · 待挂接"):@"音高 随速" forState:UIControlStateNormal];
    self.noteSpeeds.text=hud.scroll_valid?[NSString stringWithFormat:@"CurrentNoteSpeed %.2f × SPEED %.2f = NoteSpeed %.2f",hud.current_note_speed,hud.percent/100.0,hud.note_speed]:@"CurrentNoteSpeed --   ·   NoteSpeed --";
    self.compLabel.text=[NSString stringWithFormat:@"补偿 %ums",pcp_pitch_comp_get()];
    PracticePoints p=pcp_points_snapshot();unsigned extent=MAX(1u,pcp_points_extent()),current=pcp_points_current();
    self.axis.points=p;self.axis.extent=extent;self.axis.current=current;self.axis.selected=self.selected;self.axis.userInteractionEnabled=!busy&&!editing;[self.axis setNeedsDisplay];
    self.rangeLabel.text=[NSString stringWithFormat:@"C %@   ·   %@ %@",PTTime(current),p.duration?@"曲长":@"已播放范围",PTTime(extent)];
    for(unsigned i=0;i<5;i++) {
        UIButton *b=self.chips[i];[b setTitle:(p.mask&(1u<<i))?[NSString stringWithFormat:@"%@  %@",PTName(i),PTTime(p.ms[i])]:[NSString stringWithFormat:@"%@  未设置",PTName(i)] forState:UIControlStateNormal];
        b.enabled=!busy&&!editing&&(p.mask&(1u<<i));b.selected=self.selected==(NSInteger)i;b.alpha=b.enabled?1:0.45;
    }
    BOOL has=self.selected>=0 && (p.mask&(1u<<self.selected));
    self.selection.text=has?[NSString stringWithFormat:@"选中 %@ · %@ · %u ms   再次点击可编辑",PTName(self.selected),PTTime(p.ms[self.selected]),p.ms[self.selected]]:@"点击轴上或下方的 A / B / T 选中，再次点击编辑";
    [self.jump setTitle:has?[NSString stringWithFormat:@"跳转到 %@ · %@",PTName(self.selected),PTTime(p.ms[self.selected])]:@"选中点位后跳转" forState:UIControlStateNormal];
    self.jump.enabled=has&&!busy&&!editing;self.jump.alpha=self.jump.enabled?1:0.45;
    self.removeT.enabled=has&&self.selected>=2&&!busy&&editing;
    self.addT.enabled=!busy&&!editing&&((p.mask&28)!=28);
    for(UIButton *b in self.captureButtons)b.enabled=!busy&&!editing;
    self.loop.enabled=!busy&&!editing;
    NSString *loopTitle=pcp_ab_loop_get()?@"A/B 循环-开":@"A/B 循环-关";
    if(pcp_loop_suspended())loopTitle=@"A/B 循环-开 · 已暂挂";
    else if(pcp_ab_loop_get() && ((p.mask&3)!=3 || p.ms[1]<=p.ms[0]))loopTitle=@"A/B 循环-开 · 请设置 A < B";
    [self.loop setTitle:loopTitle forState:UIControlStateNormal];
    if(busy)self.status.text=phase==1?@"等待跳转条件…":phase==2?@"正在重试，等待新会话…":phase==3?@"等待预备段结束并定位…":(s.visible?@"已暂停，恢复播放后继续确认":@"正在确认跳转位置…");
    else if(CFAbsoluteTimeGetCurrent()>self.messageUntil)
        self.status.text=phase==6?[NSString stringWithFormat:@"跳转未通过验证 (%u)，循环已停止",pcp_seek_error()]:phase==5?@"已跳转 · 可继续练习":@"选点 → 再点编辑 · 跳转会重试并开始练习";
}
@end
