#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

@interface WebGpuRendererWrapper : NSObject

- (instancetype)initWithLayer:(CAMetalLayer *)layer width:(CGFloat)width height:(CGFloat)height;
- (void)resizeWithWidth:(CGFloat)width height:(CGFloat)height;
- (void)render:(float)dt;
- (void)destroy;

- (void)setPitchLock:(BOOL)locked;
- (void)setShowHint:(BOOL)show;
- (void)setDebugLogs:(BOOL)enabled;
- (void)applyAlgorithm:(NSString *)alg;
- (void)setSpeed:(float)speed;
- (void)reset;
- (BOOL)isPlaying;
- (BOOL)isAnimating;
- (void)play;
- (void)pause;
- (void)stepForward;
- (void)stepBackward;
- (void)seekFraction:(float)f;
- (float)currentFraction;

- (void)onDragBeginX:(float)x y:(float)y;
- (void)onDragMoveX:(float)x y:(float)y;
- (void)onDragEndX;
- (void)onZoom:(float)delta;
- (void)setCameraPositionLat:(float)lat lon:(float)lon rad:(float)rad;
- (void)setBackgroundColorR:(float)r g:(float)g b:(float)b a:(float)a;
- (void)setFaceColors:(NSArray<NSNumber *> *)colors;
- (void)setBodyAlpha:(float)alpha;

@end

NS_ASSUME_NONNULL_END
