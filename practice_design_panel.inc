/* Methods included inside PCPRatePanel. Coordinates follow the Studio canvas. */
- (UIView *)designed:(NSString *)key action:(SEL)action {
    NSDictionary *e=PTElement(key);CGRect frame=PTRect(e);
    if(!e)return nil;
    if(action || [e[@"type"] isEqual:@"button"]) {
        PTTextureButton *button=[[PTTextureButton alloc] initWithFrame:frame];
        button.backgroundColor=UIColor.clearColor;
        button.normalTexture=PTDesignImage(e,e[@"asset"]);
        button.pressedTexture=PTDesignImage(e,e[@"pressedAsset"]?:e[@"asset"]);
        NSString *selectionAsset=e[@"selectedAsset"];
        if(!selectionAsset || [selectionAsset isEqual:e[@"pressedAsset"]])selectionAsset=e[@"asset"];
        button.selectedTexture=PTDesignImage(e,selectionAsset);
        button.caption=[[UILabel alloc] initWithFrame:button.bounds];PTLabelStyle(button.caption,e);
        button.caption.text=e[@"text"];button.caption.userInteractionEnabled=NO;[button addSubview:button.caption];
        if(action)[button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
        [self.panel addSubview:button];return button;
    }
    UIImage *image=PTDesignImage(e,e[@"asset"]);
    if(image) {UIImageView *background=[[UIImageView alloc] initWithImage:image];background.frame=frame;background.alpha=e[@"opacity"]?[e[@"opacity"] doubleValue]:1;[self.panel addSubview:background];}
    if([e[@"type"] isEqual:@"text"]) {
        UILabel *label=[[UILabel alloc] initWithFrame:frame];PTLabelStyle(label,e);label.text=e[@"text"];
        [self.panel addSubview:label];return label;
    }
    return nil;
}
- (void)caption:(UIButton *)button design:(NSString *)key {
    PTTextureButton *b=(PTTextureButton *)button;NSDictionary *e=PTElement(key);
    CGRect r=PTRect(e);r.origin.x-=b.frame.origin.x;r.origin.y-=b.frame.origin.y;
    b.caption.frame=r;PTLabelStyle(b.caption,e);b.caption.text=e[@"text"];
}
- (void)build {
    self.selected=-1;self.overlay=[PCPRateOverlay new];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(hideForTransition) name:@"PCPTransitionBegin" object:nil];
    self.curtain=[UIView new];self.curtain.backgroundColor=[UIColor colorWithRed:0.13 green:0.10 blue:0.23 alpha:1];self.curtain.hidden=YES;[self.overlay addSubview:self.curtain];
    self.panel=[UIView new];self.panel.hidden=YES;self.panel.backgroundColor=self.curtain.backgroundColor;[self.overlay addSubview:self.panel];
    for(NSString *key in @[@"el_04556b15",@"el_c96bf939",@"el1",@"el_3d9862fb",@"el_7350ac55",@"el_ef5dfc58"])[self designed:key action:NULL];
    self.close=(UIButton *)[self designed:@"el2" action:@selector(toggle)];
    self.value=(UILabel *)[self designed:@"el3" action:NULL];

    self.noteMode=(UIButton *)[self designed:@"el11" action:@selector(noteTap)];

    UIButton *autoplay=(UIButton *)[self designed:@"el_56cb338e" action:NULL];autoplay.enabled=NO;autoplay.accessibilityLabel=@"自动播放，尚未接入";
    self.noteSpeeds=(UILabel *)[self designed:@"el13" action:NULL];
    self.noteSpeedValue=(UILabel *)[self designed:@"el_c14bfa62" action:NULL];
    self.appliedValue=(UILabel *)[self designed:@"el_bc36f37a" action:NULL];
    self.rateSteps=[NSMutableArray array];int deltas[]={-5,-1,1,5};
    NSArray *arrows=@[@"el_56f63468",@"el_52401071",@"el_172f7642",@"el_71b0f9ab"];
    for(int i=0;i<4;i++){UIButton *b=(UIButton *)[self designed:arrows[i] action:@selector(step:)];b.tag=deltas[i];[self.rateSteps addObject:b];}
    self.slider=[[PTDesignSlider alloc] initWithFrame:CGRectMake(96,284,480,48)];self.slider.minimumValue=50;self.slider.maximumValue=200;self.slider.continuous=YES;
    UIImage *track=PTDesignImage(PTElement(@"el_1c808be6"),PTElement(@"el_1c808be6")[@"asset"]);
    [self.slider setMinimumTrackImage:track forState:UIControlStateNormal];[self.slider setMaximumTrackImage:track forState:UIControlStateNormal];
    [self.slider setThumbImage:PTDesignImage(PTElement(@"el_cfd16d98"),PTElement(@"el_cfd16d98")[@"asset"]) forState:UIControlStateNormal];
    [self.slider addTarget:self action:@selector(slide:) forControlEvents:UIControlEventValueChanged];[self.panel addSubview:self.slider];
    [self designed:@"el_d23a33e0" action:NULL];
    self.noteControlValue=(UILabel *)[self designed:@"el_40530d2f" action:NULL];
    self.noteControlValue.userInteractionEnabled=YES;
    [self.noteControlValue addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(noteNative:)]];
    self.noteSteps=[NSMutableArray array];NSArray *noteIDs=@[@"el_f712d0cd",@"el_0ac5e522",@"el_55440a69",@"el_23191987"];int noteDelta[]={-50,-10,10,50};
    for(int i=0;i<4;i++){UIButton *v=(UIButton *)[self designed:noteIDs[i] action:@selector(noteStep:)];v.tag=noteDelta[i];v.accessibilityLabel=[NSString stringWithFormat:@"CNS %+.2f",noteDelta[i]/100.0];[self.noteSteps addObject:v];}
    self.noteSlider=[[PTDesignSlider alloc] initWithFrame:CGRectMake(816,284,464,48)];self.noteSlider.minimumValue=0.5;self.noteSlider.maximumValue=30;self.noteSlider.continuous=YES;
    [self.noteSlider setMinimumTrackImage:track forState:UIControlStateNormal];[self.noteSlider setMaximumTrackImage:track forState:UIControlStateNormal];
    [self.noteSlider setThumbImage:PTDesignImage(PTElement(@"el_cfd16d98"),PTElement(@"el_cfd16d98")[@"asset"]) forState:UIControlStateNormal];
    [self.noteSlider addTarget:self action:@selector(noteSlide:) forControlEvents:UIControlEventValueChanged];[self.panel addSubview:self.noteSlider];
    self.notePending=[self label:16 frame:CGRectMake(784,340,528,44) text:@"CNS · 大步 0.50 / 小步 0.10 · 重试生效"];self.notePending.numberOfLines=2;
    self.axis=[[PTTimeline alloc] initWithFrame:PTRect(PTElement(@"el14"))];self.axis.backgroundColor=UIColor.clearColor;
    __weak PCPRatePanel *weakSelf=self;self.axis.choose=^(NSUInteger i){[weakSelf choose:i];};[self.panel addSubview:self.axis];
    self.axis.drag=^(unsigned ms){[weakSelf dragTime:ms];};
    self.previousPoint=(UIButton *)[self designed:@"el_94c00307" action:@selector(cyclePoint:)];self.previousPoint.tag=-1;
    self.nextPoint=(UIButton *)[self designed:@"el_52d826d7" action:@selector(cyclePoint:)];self.nextPoint.tag=1;
    self.freeSeek=(UIButton *)[self designed:@"el_4b16cc98" action:@selector(freeSeekTap)];self.freeSeek.accessibilityLabel=@"自由定位，确认后跳转";
    self.chips=[NSMutableArray array];
    UIButton *a=(UIButton *)[self designed:@"el21" action:@selector(capture:)];a.tag=0;[self caption:a design:@"el_79bd372b"];
    UIButton *b=(UIButton *)[self designed:@"el22" action:@selector(capture:)];b.tag=1;[self caption:b design:@"el_d595675e"];
    self.captureButtons=[NSMutableArray arrayWithObjects:a,b,nil];
    self.addT=(UIButton *)[self designed:@"el23" action:@selector(addBookmark)];[self caption:self.addT design:@"el_52b9865f"];
    self.jump=(UIButton *)[self designed:@"el26" action:@selector(jumpTap)];[self caption:self.jump design:@"el_03762b62"];
    self.loop=(UIButton *)[self designed:@"el24" action:@selector(loopTap)];self.loop.accessibilityLabel=@"A/B 循环";
    self.loopState=[self label:16 frame:CGRectMake(568,754,184,24) text:@"A/B 循环-关"];
    self.selection=[self label:20 frame:CGRectMake(400,600,280,32) text:@"选择点位开始编辑"];
    self.status=[self label:17 frame:CGRectMake(400,984,280,40) text:@""];
    self.cancelJump=[self button:@"取消待执行跳转" frame:CGRectMake(400,880,272,44) action:@selector(cancelWaiting)];
    self.continueButton=[self button:@"继续准备" frame:CGRectMake(400,936,272,44) action:@selector(continuePlaying)];
    self.editBox=[[UIView alloc] initWithFrame:CGRectMake(696,616,440,216)];self.editBox.hidden=YES;[self.panel addSubview:self.editBox];
    UIImageView *inputBackground=[[UIImageView alloc] initWithImage:PTDesignImage(PTElement(@"el_bcefc7e0"),PTElement(@"el_bcefc7e0")[@"asset"])];inputBackground.frame=CGRectMake(8,0,432,56);[self.editBox addSubview:inputBackground];
    self.milliseconds=[[UILabel alloc] initWithFrame:CGRectMake(8,0,376,56)];PTLabelStyle(self.milliseconds,PTElement(@"el_bcefc7e0"));self.milliseconds.userInteractionEnabled=YES;
    [self.milliseconds addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(openDigits)]];[self.editBox addSubview:self.milliseconds];
    UILabel *unit=[[UILabel alloc] initWithFrame:CGRectMake(384,0,48,56)];unit.text=@"ms";unit.font=PTFont(22,YES);unit.textColor=UIColor.whiteColor;[self.editBox addSubview:unit];
    int steps[]={-1000,-100,100,1000};NSArray *stepIDs=@[@"el_fcb25955",@"el_4fe2b11d",@"el_24d119d5",@"el_fdf35a18"];
    self.editButtons=[NSMutableArray array];
    for(int i=0;i<4;i++){UIButton *v=(UIButton *)[self designed:stepIDs[i] action:@selector(timeStep:)];v.tag=steps[i];CGRect r=v.frame;r.origin.x-=696;r.origin.y-=616;v.frame=r;v.accessibilityLabel=[NSString stringWithFormat:@"%+d 毫秒",steps[i]];[self.editBox addSubview:v];[self.editButtons addObject:v];[v addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(holdTime:)]];}
    for(NSString *key in @[@"el_2ebea99a",@"el_c746bf87"]){UIButton *v=(UIButton *)[self designed:key action:[key isEqual:@"el_2ebea99a"]?@selector(confirmEdit):@selector(cancelEdit)];CGRect r=v.frame;r.origin.x-=696;r.origin.y-=616;v.frame=r;[self.editBox addSubview:v];}
    self.keypad=[[UIView alloc] initWithFrame:CGRectMake(696,840,440,184)];self.keypad.hidden=YES;[self.panel addSubview:self.keypad];
    NSArray *keys=@[@"1",@"2",@"3",@"4",@"5",@"6",@"7",@"8",@"9",@"清空",@"0",@"退格"];
    for(int i=0;i<12;i++){
        UIButton *v=[self button:keys[i] frame:CGRectMake((i%3)*148,(i/3)*46,144,44) action:@selector(digit:)];v.tag=i;[self.keypad addSubview:v];
    }
    self.entry=[self button:@"练习" frame:CGRectZero action:@selector(toggle)];[self.overlay addSubview:self.entry];
    self.speedHUD=(UILabel *)[self designed:@"el33" action:NULL];self.accHUD=(UILabel *)[self designed:@"el34" action:NULL];
    self.currentHUD=(UILabel *)[self designed:@"el_024b6d15" action:NULL];self.durationHUD=(UILabel *)[self designed:@"el_fd5065da" action:NULL];
    self.countdown=(UILabel *)[self designed:@"el35" action:NULL];
    for(UILabel *l in @[self.speedHUD,self.accHUD,self.currentHUD,self.durationHUD,self.countdown]){[self.overlay addSubview:l];l.hidden=YES;l.layer.shadowColor=UIColor.blackColor.CGColor;l.layer.shadowOpacity=0.5;l.layer.shadowRadius=2;l.layer.shadowOffset=CGSizeMake(0,1);}
    self.draft=pcp_rate_ui_stored_percent();[self updateDraft];
}
- (void)layoutDesignHUD {
    CGFloat scale=MIN(self.overlay.bounds.size.width/1376,self.overlay.bounds.size.height/1032);
    CGFloat dx=(self.overlay.bounds.size.width-1376*scale)/2,dy=(self.overlay.bounds.size.height-1032*scale)/2;
    NSArray *labels=@[self.speedHUD,self.accHUD,self.currentHUD,self.durationHUD,self.countdown];
    NSArray *keys=@[@"el33",@"el34",@"el_024b6d15",@"el_fd5065da",@"el35"];
    for(unsigned i=0;i<labels.count;i++){UILabel *l=labels[i];NSDictionary *e=PTElement(keys[i]);CGRect r=PTRect(e);l.frame=CGRectMake(dx+r.origin.x*scale,dy+r.origin.y*scale,r.size.width*scale,r.size.height*scale);l.font=[PTDesignFont(e) fontWithSize:[e[@"fontSize"] doubleValue]*scale];}
}
