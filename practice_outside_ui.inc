/* Outside practice settings. Preferences only, never game object mutation. */
- (void)buildOutside {
    self.outsideEntryHost=[[PCPRateOverlay alloc] initWithFrame:CGRectMake(0,0,1376,1032)];
    [self.overlay addSubview:self.outsideEntryHost];self.outsideEntryHost.hidden=YES;
    self.outsideEntry=(UIButton *)[self designed:@"el_2f376855" action:@selector(outsideToggle) parent:self.outsideEntryHost];
    self.outsideEntry.accessibilityLabel=@"局外练习设置";
    [self caption:self.outsideEntry design:@"el_f8d86e4c"];
    self.outsidePanel=[[UIView alloc] initWithFrame:CGRectMake(0,0,1376,1032)];
    self.outsidePanel.backgroundColor=[UIColor colorWithWhite:0 alpha:0.35];
    self.outsidePanel.hidden=YES;[self.overlay addSubview:self.outsidePanel];
    UIButton *dismiss=[[UIButton alloc] initWithFrame:self.outsidePanel.bounds];
    dismiss.accessibilityLabel=@"关闭局外练习设置";
    [dismiss addTarget:self action:@selector(outsideClose) forControlEvents:UIControlEventTouchUpInside];
    [self.outsidePanel addSubview:dismiss];
    self.outsideViews=[NSMutableDictionary dictionary];
    NSArray *rateIDs=@[@"el_2f79b0c4",@"el_3311a015",@"el_0453e383",@"el_9dc1d38c"];
    NSArray *noteIDs=@[@"el_129caae6",@"el_400594d7",@"el_e42e438b",@"el_3d713da5"];
    NSArray *unavailable=@[@"el_8670e9d4",@"el_d2a36ce7",@"el_13dad963",@"el_66bf3b78",@"el_dfd77ec5",@"el_085dcf7a",@"el_a43a4aff",@"el_ddfed51f"];
    NSArray *judgeValues=@[@"el_f50a0d7f",@"el_4aecaa0a",@"el_7e571e02",@"el_e1e84d07"];
    for(NSString *key in PTDesign()[@"_outsideOrder"]) {
        NSDictionary *element=PTElement(key);
        if([@[@"runtime_outside_dimmer",@"runtime_outside_status",@"el_6cd71203",@"el_067b5fe5"] containsObject:key])continue;
        if([element[@"hidden"] boolValue] || [element[@"visibleWhen"] isEqual:@"editing"])continue;
        SEL action=NULL;NSInteger tag=0;
        NSUInteger i=[rateIDs indexOfObject:key];
        if(i!=NSNotFound){action=@selector(outsideRate:);int steps[]={-5,-1,1,5};tag=steps[i];}
        i=[noteIDs indexOfObject:key];
        if(i!=NSNotFound){action=@selector(outsideNote:);int steps[]={-50,-10,10,50};tag=steps[i];}
        if([key isEqual:@"el_ccb9ac7b"])action=@selector(outsideMode);
        if([key isEqual:@"el_17314d40"])action=@selector(outsidePitch);
        if([key isEqual:@"el_865d06f7"])action=@selector(outsideLog);
        if([key isEqual:@"el_09e71c48"])action=@selector(hudTap);
        UIView *view=[self designed:key action:action parent:self.outsidePanel];
        if(view)self.outsideViews[key]=view;
        if([view isKindOfClass:UIButton.class])((UIButton *)view).tag=tag;
        if([unavailable containsObject:key]) {
            UIButton *button=(UIButton *)view;button.enabled=NO;

        }
        if([judgeValues containsObject:key])((UILabel *)view).text=@"--";

    }
    self.outsideStatus=(UILabel *)[self designed:@"runtime_outside_status" action:NULL parent:self.outsidePanel];
    if(!self.outsideStatus) {
        self.outsideStatus=[[UILabel alloc] initWithFrame:CGRectMake(96,880,1184,32)];
        self.outsideStatus.font=PTFont(19,NO);self.outsideStatus.textColor=UIColor.whiteColor;
        self.outsideStatus.textAlignment=NSTextAlignmentCenter;[self.outsidePanel addSubview:self.outsideStatus];
    }
    UILabel *number=(UILabel *)self.outsideViews[@"el_745197c4"];number.userInteractionEnabled=YES;
    [number addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(outsideNative:)]];
}
- (void)outsideClose {self.outsidePanel.hidden=YES;}
- (BOOL)outsideCanEdit {
    return !self.outsidePanel.hidden && self.outsideContext && pcp_outside_context()==self.outsideContext;
}
- (void)outsideToggle {
    if(!self.outsideContext || pcp_outside_context()!=self.outsideContext)return;
    self.outsidePanel.hidden=!self.outsidePanel.hidden;[self outsideRefresh];
}
- (void)outsideMessage:(NSString *)message {
    self.outsideStatus.text=message;self.outsideMessageUntil=CFAbsoluteTimeGetCurrent()+4;
}
- (double)outsideReference {
    unsigned custom=pcp_note_reference_get();
    if(custom)return custom/1000.0;
    double native=[NSUserDefaults.standardUserDefaults doubleForKey:@"PCPLastNativeNote"];
    return isfinite(native)&&native>=0.1&&native<=30?native:0;
}
- (void)outsideRate:(UIButton *)button {
    if(![self outsideCanEdit])return;
    NSInteger rate=MAX(50,MIN(200,(NSInteger)pcp_rate_ui_stored_percent()+button.tag));
    pcp_rate_ui_save_percent((unsigned)rate);[self outsideRefresh];
}
- (void)outsideNote:(UIButton *)button {
    if(![self outsideCanEdit] || pcp_note_mode_get())return;
    double base=[self outsideReference];if(base<=0)return;
    pcp_note_current_set(MAX(0.5,MIN(30,base+button.tag/100.0)));[self outsideRefresh];
}
- (void)outsideNative:(UILongPressGestureRecognizer *)gesture {
    if(gesture.state!=UIGestureRecognizerStateBegan || ![self outsideCanEdit] || pcp_note_mode_get())return;
    pcp_note_native_reset();[self outsideRefresh];[self outsideMessage:@"已恢复跟随游戏流速，下次进歌读取实际值"];
}
- (void)outsideMode {
    if(![self outsideCanEdit])return;
    pcp_note_mode_set(!pcp_note_mode_get());[self outsideRefresh];
}
- (void)outsidePitch {
    if(![self outsideCanEdit])return;
    pcp_pitch_mode_set(!pcp_pitch_mode_get());[self outsideRefresh];
}
- (void)outsideLog {
    if(![self outsideCanEdit])return;
    [self outsideMessage:pcp_log_refresh()?@"已备份日志并开始新日志，诊断计数继续累计":@"日志刷新失败，原日志已保留或备份"];
}
- (void)outsideRefresh {
    unsigned rate=pcp_rate_ui_stored_percent(),mode=pcp_note_mode_get();
    double reference=[self outsideReference],current=reference*(mode?100.0/rate:1.0);
    ((UILabel *)self.outsideViews[@"el_3943f258"]).text=[NSString stringWithFormat:@"%.2fx",rate/100.0];
    ((UILabel *)self.outsideViews[@"el_96eb867d"]).text=[NSString stringWithFormat:@"SPEED %.2fx",rate/100.0];
    ((UILabel *)self.outsideViews[@"el_745197c4"]).text=reference>0?[NSString stringWithFormat:@"%.3f",current]:@"--";
    ((UILabel *)self.outsideViews[@"el_e6794a72"]).text=reference>0?[NSString stringWithFormat:@"CurrentNoteSpeed %.3f",current]:@"CNS 跟随游戏";
    ((UILabel *)self.outsideViews[@"el_fc9dce22"]).text=reference>0?[NSString stringWithFormat:@"NoteSpeed %.3f",current*rate/100.0]:@"NoteSpeed --";
    [(UIButton *)self.outsideViews[@"el_ccb9ac7b"] setTitle:mode?@"流速不变":@"同步变速" forState:UIControlStateNormal];
    [(UIButton *)self.outsideViews[@"el_17314d40"] setTitle:pcp_pitch_mode_get()?@"音高保持":@"音高随速" forState:UIControlStateNormal];
    [(UIButton *)self.outsideViews[@"el_09e71c48"] setTitle:[self hudEnabled]?@"HUD显示":@"HUD隐藏" forState:UIControlStateNormal];
    for(NSString *key in @[@"el_129caae6",@"el_400594d7",@"el_e42e438b",@"el_3d713da5"])
        ((UIButton *)self.outsideViews[key]).enabled=!mode&&reference>0;
    if(CFAbsoluteTimeGetCurrent()>self.outsideMessageUntil)
        self.outsideStatus.text=reference<=0?@"设置自动保存，下次进歌生效 · 首次游玩后可读取基准流速 · 点击背景返回":
            (pcp_note_reference_get()?@"局外预设自动保存，下次进歌生效 · 点击背景返回":@"流速预览使用上次基准，下次进歌重新读取 · 点击背景返回");
}
- (BOOL)updateOutside:(UIWindow *)window {
    uint64_t context=pcp_outside_context();
    if(!context){self.outsideContext=0;self.outsidePanel.hidden=self.outsideEntryHost.hidden=YES;return NO;}
    if(context!=self.outsideContext){self.outsideContext=context;self.outsideSince=CFAbsoluteTimeGetCurrent();self.outsidePanel.hidden=YES;}
    [self hideForTransition];self.overlay.hidden=NO;
    self.outsideEntryHost.hidden=CFAbsoluteTimeGetCurrent()-self.outsideSince<1.0;
    CGFloat scale=MIN(window.bounds.size.width/1376,window.bounds.size.height/1032);
    for(UIView *view in @[self.outsideEntryHost,self.outsidePanel]) {
        view.transform=CGAffineTransformIdentity;view.bounds=CGRectMake(0,0,1376,1032);
        view.transform=CGAffineTransformMakeScale(scale,scale);view.center=CGPointMake(window.bounds.size.width/2,window.bounds.size.height/2);
    }
    if(!self.outsidePanel.hidden)[self outsideRefresh];return YES;
}
