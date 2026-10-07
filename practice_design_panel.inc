/* Methods included inside PCPRatePanel. Coordinates follow the Studio canvas. */
- (UIView *)designed:(NSString *)key action:(SEL)action {
    return [self designed:key action:action parent:self.panel];
}
- (UIView *)designed:(NSString *)key action:(SEL)action parent:(UIView *)parent {
    NSDictionary *e=PTElement(key);CGRect frame=PTRect(e);
    if(!e || [e[@"hidden"] boolValue])return nil;
    if(action || [e[@"type"] isEqual:@"button"]) {
        PTTextureButton *button=[[PTTextureButton alloc] initWithFrame:frame];
        button.backgroundColor=UIColor.clearColor;
        button.normalTexture=PTDesignImage(e,e[@"asset"]);
        button.pressedTexture=PTDesignImage(e,e[@"pressedAsset"]?:e[@"asset"]);
        NSString *selectionAsset=e[@"selectedAsset"];
        if(!selectionAsset)selectionAsset=e[@"asset"];
        button.selectedTexture=PTDesignImage(e,selectionAsset);
        button.caption=[[UILabel alloc] initWithFrame:button.bounds];PTLabelStyle(button.caption,e);
        button.caption.text=e[@"text"];button.caption.userInteractionEnabled=NO;[button addSubview:button.caption];
        button.designOpacity=e[@"opacity"]?[e[@"opacity"] doubleValue]:1;button.alpha=button.designOpacity;
        if(action)[button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
        [parent addSubview:button];return button;
    }
    UIImage *image=PTDesignImage(e,e[@"asset"]);
    UIImageView *background=nil;
    if(image) {background=[[UIImageView alloc] initWithImage:image];background.frame=frame;background.alpha=e[@"opacity"]?[e[@"opacity"] doubleValue]:1;[parent addSubview:background];}
    if([e[@"type"] isEqual:@"text"]) {
        UILabel *label=[[UILabel alloc] initWithFrame:frame];PTLabelStyle(label,e);label.text=e[@"text"];
        label.alpha=e[@"opacity"]?[e[@"opacity"] doubleValue]:1;
        [parent addSubview:label];return label;
    }
    return background;
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
    for(NSString *key in @[@"el_04556b15",@"el_c96bf939",@"el_c58bbc86",@"el_f0498a5f",@"el1",@"el_3d9862fb",@"el_7350ac55",@"el_ef5dfc58"])[self designed:key action:NULL];
    self.close=(UIButton *)[self designed:@"el2" action:@selector(toggle)];
    self.value=(UILabel *)[self designed:@"el3" action:NULL];

    self.noteMode=(UIButton *)[self designed:@"el11" action:@selector(noteTap)];

    UIButton *animation=(UIButton *)[self designed:@"el_ef4c027c" action:NULL];animation.enabled=NO;animation.accessibilityLabel=@"动画同步变速";
    self.hudMode=(UIButton *)[self designed:@"el_700100de" action:@selector(hudTap)];
    UIButton *autoplay=(UIButton *)[self designed:@"el_56cb338e" action:NULL];autoplay.enabled=NO;autoplay.accessibilityLabel=@"自动播放";
    self.noteSpeeds=(UILabel *)[self designed:@"el13" action:NULL];
    self.noteSpeedValue=(UILabel *)[self designed:@"el_c14bfa62" action:NULL];
    self.appliedValue=(UILabel *)[self designed:@"el_bc36f37a" action:NULL];
    self.rateSteps=[NSMutableArray array];int deltas[]={-5,-1,1,5};
    NSArray *arrows=@[@"el_56f63468",@"el_52401071",@"el_172f7642",@"el_71b0f9ab"];
    for(int i=0;i<4;i++){UIButton *b=(UIButton *)[self designed:arrows[i] action:@selector(step:)];b.tag=deltas[i];[self.rateSteps addObject:b];}
    self.slider=[[PTDesignSlider alloc] initWithFrame:PTSliderFrame(@"el_1c808be6")];self.slider.minimumValue=50;self.slider.maximumValue=200;self.slider.continuous=YES;
    UIImage *track=PTDesignImage(PTElement(@"el_1c808be6"),PTElement(@"el_1c808be6")[@"asset"]);
    [self.slider setMinimumTrackImage:track forState:UIControlStateNormal];[self.slider setMaximumTrackImage:track forState:UIControlStateNormal];
    [self.slider setThumbImage:PTDesignImage(PTElement(@"el_cfd16d98"),PTElement(@"el_cfd16d98")[@"asset"]) forState:UIControlStateNormal];
    [self.slider addTarget:self action:@selector(slide:) forControlEvents:UIControlEventValueChanged];[self.panel addSubview:self.slider];
    [self designed:@"el_d23a33e0" action:NULL];
    self.noteControlValue=(UILabel *)[self designed:@"el_40530d2f" action:NULL];
    self.noteControlValue.userInteractionEnabled=YES;
    [self.noteControlValue addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(noteNative:)]];
    self.noteSteps=[NSMutableArray array];NSArray *noteIDs=@[@"el_f712d0cd",@"el_0ac5e522",@"el_55440a69",@"el_23191987"];int noteDelta[]={-10,-50,50,10};
    for(int i=0;i<4;i++){UIButton *v=(UIButton *)[self designed:noteIDs[i] action:@selector(noteStep:)];v.tag=noteDelta[i];v.accessibilityLabel=[NSString stringWithFormat:@"CNS %+.2f",noteDelta[i]/100.0];[self.noteSteps addObject:v];}
    self.noteSlider=[[PTDesignSlider alloc] initWithFrame:PTSliderFrame(@"el_306912e1")];self.noteSlider.minimumValue=0.5;self.noteSlider.maximumValue=30;self.noteSlider.continuous=YES;
    [self.noteSlider setMinimumTrackImage:track forState:UIControlStateNormal];[self.noteSlider setMaximumTrackImage:track forState:UIControlStateNormal];
    [self.noteSlider setThumbImage:PTDesignImage(PTElement(@"el_f7b1264e"),PTElement(@"el_f7b1264e")[@"asset"]) forState:UIControlStateNormal];
    [self designed:@"el_03c97d24" action:NULL];[self designed:@"el_3a705b55" action:NULL];
    [self.noteSlider addTarget:self action:@selector(noteSlide:) forControlEvents:UIControlEventValueChanged];[self.panel addSubview:self.noteSlider];
    self.axis=[[PTTimeline alloc] initWithFrame:PTRect(PTElement(@"el14"))];self.axis.backgroundColor=UIColor.clearColor;
    __weak PCPRatePanel *weakSelf=self;self.axis.choose=^(NSUInteger i){[weakSelf choose:i];};[self.panel addSubview:self.axis];
    self.axis.drag=^(unsigned ms){[weakSelf dragTime:ms];};
    self.previousPoint=(UIButton *)[self designed:@"el_94c00307" action:@selector(cyclePoint:)];self.previousPoint.tag=-1;
    self.nextPoint=(UIButton *)[self designed:@"el_52d826d7" action:@selector(cyclePoint:)];self.nextPoint.tag=1;
    self.chips=[NSMutableArray array];
    UIButton *a=(UIButton *)[self designed:@"el21" action:@selector(capture:)];a.tag=0;[self caption:a design:@"el_79bd372b"];
    UIButton *b=(UIButton *)[self designed:@"el22" action:@selector(capture:)];b.tag=1;[self caption:b design:@"el_d595675e"];
    self.captureButtons=[NSMutableArray arrayWithObjects:a,b,nil];
    self.addT=(UIButton *)[self designed:@"el23" action:@selector(addBookmark)];[self caption:self.addT design:@"el_52b9865f"];
    self.removeT=(UIButton *)[self designed:@"el26" action:@selector(deletePoint)];[self caption:self.removeT design:@"el_03762b62"];
    self.jump=(UIButton *)[self designed:@"el_e8b2af72" action:@selector(jumpTap)];[self caption:self.jump design:@"el_2539024f"];
    self.pitchMode=(UIButton *)[self designed:@"el_3f09d6b9" action:@selector(pitchTap)];[self caption:self.pitchMode design:@"el_c161b7ae"];
    UIButton *countdownSetting=(UIButton *)[self designed:@"el_e843b58e" action:NULL];[self caption:countdownSetting design:@"el_6db65912"];countdownSetting.enabled=NO;
    for(NSString *key in @[@"el_f3725514",@"el_3e70b005",@"el_f5711c49",@"el_c09a387f",@"el_206df674",@"el_2a700caf"])[self designed:key action:NULL];
    self.loop=(UIButton *)[self designed:@"el_63d4d70e" action:@selector(loopTap)];self.loop.accessibilityLabel=@"A/B 循环";
    self.selection=(UILabel *)[self designed:@"runtime_selection" action:NULL];
    self.status=(UILabel *)[self designed:@"runtime_status" action:NULL];self.status.text=@"";
    self.editBox=[[PCPRateOverlay alloc] initWithFrame:CGRectMake(0,0,1376,1032)];self.editBox.hidden=NO;[self.panel addSubview:self.editBox];
    NSDictionary *inputDesign=PTElement(@"el_bcefc7e0");CGRect inputFrame=PTRect(inputDesign);
    UIImageView *inputBackground=[[UIImageView alloc] initWithImage:PTDesignImage(inputDesign,inputDesign[@"asset"])];inputBackground.frame=inputFrame;[self.editBox addSubview:inputBackground];
    NSDictionary *numberDesign=PTElement(@"el_5b41cc14");CGRect numberFrame=PTRect(numberDesign);
    
    self.milliseconds=[[UILabel alloc] initWithFrame:numberFrame];PTLabelStyle(self.milliseconds,numberDesign);self.milliseconds.userInteractionEnabled=NO;
    [self.editBox addSubview:self.milliseconds];
    self.freeSeek=(UIButton *)[self designed:@"el_4b16cc98" action:@selector(freeSeekTap) parent:self.editBox];self.freeSeek.accessibilityLabel=@"自由定位";
    NSDictionary *unitDesign=PTElement(@"el_b2f73a7f");CGRect unitFrame=PTRect(unitDesign);
    
    UILabel *unit=[[UILabel alloc] initWithFrame:unitFrame];PTLabelStyle(unit,unitDesign);unit.text=@"ms";[self.editBox addSubview:unit];
    int steps[]={-1000,-100,100,1000};NSArray *stepIDs=@[@"el_fcb25955",@"el_4fe2b11d",@"el_24d119d5",@"el_fdf35a18"];
    self.editButtons=[NSMutableArray array];
    for(int i=0;i<4;i++){UIButton *v=(UIButton *)[self designed:stepIDs[i] action:@selector(timeStep:)];v.tag=steps[i];CGRect r=v.frame;v.frame=r;v.accessibilityLabel=[NSString stringWithFormat:@"%+d 毫秒",steps[i]];[self.editBox addSubview:v];[self.editButtons addObject:v];[v addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(holdTime:)]];}
    self.editCommitButtons=[NSMutableArray array];
    for(NSString *key in @[@"el_2ebea99a",@"el_c746bf87"]){UIButton *v=(UIButton *)[self designed:key action:[key isEqual:@"el_2ebea99a"]?@selector(confirmEdit):@selector(cancelEdit)];[self caption:v design:[key isEqual:@"el_2ebea99a"]?@"el_52a664bd":@"el_9bbbe20d"];CGRect r=v.frame;v.frame=r;[self.editBox addSubview:v];[self.editCommitButtons addObject:v];}
    self.entry=[self button:@"练习" frame:CGRectZero action:@selector(toggle)];[self.overlay addSubview:self.entry];
    self.speedHUD=(UILabel *)[self designed:@"el33" action:NULL];self.accHUD=(UILabel *)[self designed:@"el34" action:NULL];
    self.currentHUD=(UILabel *)[self designed:@"el_024b6d15" action:NULL];self.durationHUD=(UILabel *)[self designed:@"el_fd5065da" action:NULL];
    self.countdown=(UILabel *)[self designed:@"el35" action:NULL];
    for(UILabel *l in @[self.speedHUD,self.accHUD,self.currentHUD,self.durationHUD,self.countdown]){[self.overlay addSubview:l];l.hidden=YES;l.layer.shadowColor=UIColor.blackColor.CGColor;l.layer.shadowOpacity=0.5;l.layer.shadowRadius=2;l.layer.shadowOffset=CGSizeMake(0,1);}
    self.hudProgressViews=[NSMutableArray array];
    for(NSString *key in @[@"el_e6b8d233",@"el_2ce63cf8",@"el_d0b61598",@"el_d7b9d62f",@"el_2729917a"]){UIView *view=[self designed:key action:NULL parent:self.overlay];if(view){view.hidden=YES;[self.hudProgressViews addObject:view];if([key isEqual:@"el_2729917a"])self.hudProgressThumb=view;}}
    self.speedHUD.userInteractionEnabled=YES;[self.speedHUD addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(speedTap)]];
    self.draft=pcp_rate_ui_stored_percent();[self updateDraft];
    [self buildOutside];
}
- (void)layoutDesignHUD {
    CGFloat scale=MIN(self.overlay.bounds.size.width/1376,self.overlay.bounds.size.height/1032);
    CGFloat dx=(self.overlay.bounds.size.width-1376*scale)/2,dy=(self.overlay.bounds.size.height-1032*scale)/2;
    NSArray *labels=@[self.speedHUD,self.accHUD,self.currentHUD,self.durationHUD,self.countdown];
    NSArray *keys=@[@"el33",@"el34",@"el_024b6d15",@"el_fd5065da",@"el35"];
    for(unsigned i=0;i<labels.count;i++){UILabel *l=labels[i];NSDictionary *e=PTElement(keys[i]);CGRect r=PTRect(e);l.frame=CGRectMake(dx+r.origin.x*scale,dy+r.origin.y*scale,r.size.width*scale,r.size.height*scale);l.font=[PTDesignFont(e) fontWithSize:[e[@"fontSize"] doubleValue]*scale];}
    NSArray *progressKeys=@[@"el_e6b8d233",@"el_2ce63cf8",@"el_d0b61598",@"el_d7b9d62f",@"el_2729917a"];
    for(NSUInteger i=0;i<self.hudProgressViews.count && i<progressKeys.count;i++) {
        UIView *view=self.hudProgressViews[i];NSDictionary *e=PTElement(progressKeys[i]);CGRect r=PTRect(e);
        if(view==self.hudProgressThumb) {
            CGRect track=PTRect(PTElement(@"el_2ce63cf8"));
            CGFloat ratio=MIN(1,MAX(0,((double)pcp_hud_state().percent-50)/150));
            r.origin.x=track.origin.x+track.size.width*ratio-r.size.width/2;
        }
        view.frame=CGRectMake(dx+r.origin.x*scale,dy+r.origin.y*scale,r.size.width*scale,r.size.height*scale);
    }
}
