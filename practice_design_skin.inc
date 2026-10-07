/* UIKit renderer for the approved Studio resource references. */
#include "practice_design_manifest.inc"
static NSDictionary *PTElement(NSString *key) { return PTDesign()[key]; }
static CGRect PTRect(NSDictionary *e) {
    return CGRectMake([e[@"x"] doubleValue],[e[@"y"] doubleValue],[e[@"w"] doubleValue],[e[@"h"] doubleValue]);
}
static CGRect PTSliderFrame(NSString *key) {
    CGRect r=PTRect(PTElement(key));return CGRectMake(r.origin.x-12,CGRectGetMidY(r)-24,r.size.width+24,48);
}
static UIFont *PTDesignFont(NSDictionary *e) {
    NSString *path=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:e[@"font"]?:@""];
    static NSMutableDictionary *fontNames;static dispatch_once_t once;
    dispatch_once(&once, ^{fontNames=[NSMutableDictionary dictionary];});
    NSString *name=fontNames[path];
    if(!name) {
        NSArray *descriptors=CFBridgingRelease(CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)[NSURL fileURLWithPath:path]));
        if(descriptors.count)name=CFBridgingRelease(CTFontDescriptorCopyAttribute((__bridge CTFontDescriptorRef)descriptors[0],kCTFontNameAttribute));
        if(!name)name=[[e[@"font"] lastPathComponent] stringByDeletingPathExtension];
        if(name)fontNames[path]=name;
    }
    CGFloat size=[e[@"fontSize"] doubleValue];
    CGFloat weight=([name containsString:@"SemiBold"] || [name containsString:@"DemiBold"])?UIFontWeightSemibold:[name containsString:@"Bold"]?UIFontWeightBold:[name containsString:@"Medium"]?UIFontWeightMedium:[name containsString:@"Light"]?UIFontWeightLight:UIFontWeightRegular;
    UIFont *font=[UIFont fontWithName:name size:size]?:[UIFont systemFontOfSize:size weight:weight];
    UIFontDescriptor *fallback=[UIFont systemFontOfSize:size weight:weight].fontDescriptor;
    return [UIFont fontWithDescriptor:[font.fontDescriptor fontDescriptorByAddingAttributes:@{UIFontDescriptorCascadeListAttribute:@[fallback]}] size:size];
}
static UIImage *PTAsset(NSString *path) {
    if(!path.length)return nil;
    static NSCache *cache;static dispatch_once_t once;
    dispatch_once(&once, ^{cache=[NSCache new];cache.totalCostLimit=48*1024*1024;});
    UIImage *image=[cache objectForKey:path];if(image)return image;
    image=[UIImage imageWithContentsOfFile:[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:path]];
    if(image)[cache setObject:image forKey:path cost:(NSUInteger)(image.size.width*image.size.height*4)];
    return image;
}
static UIImage *PTDesignImage(NSDictionary *e,NSString *path) {
    UIImage *image=PTAsset(path);if(!image)return nil;
    NSDictionary *crop=e[@"crop"];
    if(crop) {
        CGRect r=CGRectMake([crop[@"x"] doubleValue],[crop[@"y"] doubleValue],[crop[@"w"] doubleValue],[crop[@"h"] doubleValue]);
        r=CGRectIntersection(r,CGRectMake(0,0,CGImageGetWidth(image.CGImage),CGImageGetHeight(image.CGImage)));
        if(!CGRectIsEmpty(r)) {CGImageRef part=CGImageCreateWithImageInRect(image.CGImage,r);if(part){image=[UIImage imageWithCGImage:part];CGImageRelease(part);}}
    }
    CGSize size=PTRect(e).size;if(size.width<=0 || size.height<=0)return nil;
    UIGraphicsBeginImageContextWithOptions(size,NO,0);
    CGContextRef c=UIGraphicsGetCurrentContext();
    CGContextTranslateCTM(c,size.width/2,size.height/2);
    CGContextRotateCTM(c,[e[@"rotation"] doubleValue]*M_PI/180);
    CGContextScaleCTM(c,[e[@"flipH"] boolValue]?-1:1,[e[@"flipV"] boolValue]?-1:1);
    CGFloat scale=[e[@"fit"] isEqual:@"cover"]?MAX(size.width/image.size.width,size.height/image.size.height):MIN(size.width/image.size.width,size.height/image.size.height);
    CGSize target=CGSizeMake(image.size.width*scale,image.size.height*scale);
    if([e[@"fit"] isEqual:@"stretch"])target=size;
    [image drawInRect:CGRectMake(-target.width/2,-target.height/2,target.width,target.height)];
    UIImage *result=UIGraphicsGetImageFromCurrentImageContext();UIGraphicsEndImageContext();return result;
}
static void PTLabelStyle(UILabel *label,NSDictionary *e) {
    label.font=PTDesignFont(e);label.textColor=UIColor.whiteColor;
    NSString *align=e[@"textAlign"];
    label.textAlignment=[align isEqual:@"start"]?NSTextAlignmentLeft:[align isEqual:@"end"]?NSTextAlignmentRight:NSTextAlignmentCenter;
    label.adjustsFontSizeToFitWidth=YES;label.minimumScaleFactor=0.7;
}
@interface PTTextureButton : UIButton
@property(nonatomic,strong) UILabel *caption;
@property(nonatomic,strong) UIImage *normalTexture,*pressedTexture,*selectedTexture;
@property(nonatomic) BOOL unset;
@property(nonatomic) CGFloat designOpacity;
@end
@implementation PTTextureButton
- (void)drawRect:(CGRect)rect {
    UIImage *texture=(self.highlighted||self.unset)?self.pressedTexture:self.selected?self.selectedTexture:self.normalTexture;
    [(texture?:self.normalTexture) drawInRect:self.bounds];

}
- (void)setTitle:(NSString *)title forState:(UIControlState)state {if(state==UIControlStateNormal)self.caption.text=title;}
- (void)setEnabled:(BOOL)value {[super setEnabled:value];self.alpha=value?self.designOpacity:0.32;}
- (void)setHighlighted:(BOOL)value {[super setHighlighted:value];[self setNeedsDisplay];}
- (void)setSelected:(BOOL)value {
    [super setSelected:value];
    self.layer.shadowOpacity=0;
    [self setNeedsDisplay];
}
- (void)setUnset:(BOOL)value {_unset=value;[self setNeedsDisplay];}
@end
@interface PTDesignSlider : UISlider @end
@implementation PTDesignSlider
- (CGRect)trackRectForBounds:(CGRect)bounds {return CGRectMake(12,(bounds.size.height-8)/2,bounds.size.width-24,8);}
@end
