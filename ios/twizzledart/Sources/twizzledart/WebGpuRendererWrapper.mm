#import "WebGpuRendererWrapper.h"
#include "renderer/WebGpuRenderer.hpp"
#include <string>

@implementation WebGpuRendererWrapper {
    WebGpuRenderer* _renderer;
}

- (instancetype)initWithLayer:(CAMetalLayer *)layer width:(CGFloat)width height:(CGFloat)height {
    self = [super init];
    if (self) {
        _renderer = new WebGpuRenderer();
        SurfaceDescriptor desc;
        desc.metalLayer = (__bridge void*)layer;
        desc.width = (uint32_t)width;
        desc.height = (uint32_t)height;
        _renderer->init(desc);
    }
    return self;
}

- (void)dealloc {
    if (_renderer) {
        _renderer->destroy();
        delete _renderer;
        _renderer = nullptr;
    }
}

- (void)resizeWithWidth:(CGFloat)width height:(CGFloat)height {
    if (_renderer) _renderer->resize((uint32_t)width, (uint32_t)height);
}

- (void)render:(float)dt {
    if (_renderer) _renderer->render(dt);
}

- (void)destroy {
    if (_renderer) {
        _renderer->destroy();
        delete _renderer;
        _renderer = nullptr;
    }
}

- (void)setPitchLock:(BOOL)locked {
    if (_renderer) _renderer->setPitchLock(locked);
}

- (void)setShowHint:(BOOL)show {
    if (_renderer) _renderer->setShowHint(show);
}

- (void)setDebugLogs:(BOOL)enabled {
    if (_renderer) _renderer->setDebugLogs(enabled);
}

- (void)applyAlgorithm:(NSString *)alg {
    if (_renderer) _renderer->applyAlgorithm([alg UTF8String]);
}

- (void)setSpeed:(float)speed {
    if (_renderer) _renderer->setSpeed(speed);
}

- (void)reset {
    if (_renderer) _renderer->reset();
}

- (BOOL)isPlaying {
    return _renderer ? _renderer->isPlaying() : NO;
}

- (BOOL)isAnimating {
    return _renderer ? _renderer->isAnimating() : NO;
}

- (void)play {
    if (_renderer) _renderer->play();
}

- (void)pause {
    if (_renderer) _renderer->pause();
}

- (void)stepForward {
    if (_renderer) _renderer->stepForward();
}

- (void)stepBackward {
    if (_renderer) _renderer->stepBackward();
}

- (void)seekFraction:(float)f {
    if (_renderer) _renderer->seekFraction(f);
}

- (float)currentFraction {
    return _renderer ? _renderer->currentFraction() : 0.0f;
}

- (void)onDragBeginX:(float)x y:(float)y {
    if (_renderer) _renderer->onDragBegin(x, y);
}

- (void)onDragMoveX:(float)x y:(float)y {
    if (_renderer) _renderer->onDragMove(x, y);
}

- (void)onDragEndX {
    if (_renderer) _renderer->onDragEnd();
}

- (void)onZoom:(float)delta {
    if (_renderer) _renderer->onZoom(delta);
}

- (void)setCameraPositionLat:(float)lat lon:(float)lon rad:(float)rad {
    if (_renderer) _renderer->setCameraPosition(lat, lon, rad);
}

- (void)setBackgroundColorR:(float)r g:(float)g b:(float)b a:(float)a {
    if (_renderer) _renderer->setBackgroundColor(r, g, b, a);
}

- (void)setFaceColors:(NSArray<NSNumber *> *)colors {
    if (!_renderer || colors.count != 18) return;
    float c[6][3];
    for (int i = 0; i < 6; i++) {
        c[i][0] = [colors[i*3] floatValue];
        c[i][1] = [colors[i*3 + 1] floatValue];
        c[i][2] = [colors[i*3 + 2] floatValue];
    }
    _renderer->setFaceColors(c);
}

- (void)setBodyAlpha:(float)alpha {
    if (_renderer) _renderer->setBodyAlpha(alpha);
}

@end
