import Flutter
import UIKit
import Metal

public class TwizzledartPlugin: NSObject, FlutterPlugin {
  public static func register(with registrar: FlutterPluginRegistrar) {
    let factory = TwizzleViewFactory(messenger: registrar.messenger())
    registrar.register(factory, withId: "twizzle_view")
  }
}

class TwizzleViewFactory: NSObject, FlutterPlatformViewFactory {
    private var messenger: FlutterBinaryMessenger

    init(messenger: FlutterBinaryMessenger) {
        self.messenger = messenger
        super.init()
    }

    func create(
        withFrame frame: CGRect,
        viewIdentifier viewId: Int64,
        arguments args: Any?
    ) -> FlutterPlatformView {
        return TwizzlePlatformView(
            frame: frame,
            viewIdentifier: viewId,
            arguments: args,
            binaryMessenger: messenger)
    }

    public func createArgsCodec() -> FlutterMessageCodec & NSObjectProtocol {
        return FlutterStandardMessageCodec.sharedInstance()
    }
}

class MetalView: UIView {
    var onResize: ((CGFloat, CGFloat) -> Void)?
    
    override class var layerClass: AnyClass {
        return CAMetalLayer.self
    }
    
    var metalLayer: CAMetalLayer {
        return layer as! CAMetalLayer
    }
    
    override func layoutSubviews() {
        super.layoutSubviews()
        // multiply by scale for high-DPI (Retina)
        let scale = UIScreen.main.nativeScale
        metalLayer.drawableSize = CGSize(width: bounds.width * scale, height: bounds.height * scale)
        onResize?(bounds.width * scale, bounds.height * scale)
    }
}

class TwizzlePlatformView: NSObject, FlutterPlatformView, UIGestureRecognizerDelegate {
    private var _view: MetalView
    private var renderer: WebGpuRendererWrapper?
    private var displayLink: CADisplayLink?
    private var lastTime: CFTimeInterval = 0
    private var channel: FlutterMethodChannel
    
    private var touchEnabled = true
    private var wasScaling = false
    private var scaleLastSpan: CGFloat = 0

    init(
        frame: CGRect,
        viewIdentifier viewId: Int64,
        arguments args: Any?,
        binaryMessenger messenger: FlutterBinaryMessenger
    ) {
        _view = MetalView(frame: frame)
        _view.isOpaque = false
        _view.backgroundColor = .clear
        
        channel = FlutterMethodChannel(name: "twizzle_view_\(viewId)", binaryMessenger: messenger)
        super.init()
        
        let scale = UIScreen.main.nativeScale
        let w = frame.size.width * scale
        let h = frame.size.height * scale
        _view.metalLayer.drawableSize = CGSize(width: w, height: h)
        _view.metalLayer.isOpaque = false
        
        renderer = WebGpuRendererWrapper(layer: _view.metalLayer, width: w, height: h)
        
        _view.onResize = { [weak self] width, height in
            self?.renderer?.resize(withWidth: width, height: height)
        }
        
        if let params = args as? [String: Any] {
            if let bg = params["backgroundColor"] as? [NSNumber], bg.count >= 4 {
                renderer?.setBackgroundColorR(bg[0].floatValue, g: bg[1].floatValue, b: bg[2].floatValue, a: bg[3].floatValue)
            }
            if let lock = params["pitchLock"] as? Bool { renderer?.setPitchLock(lock) }
            if let dl = params["debugLogs"] as? Bool { renderer?.setDebugLogs(dl) }
            if let sh = params["showHint"] as? Bool { renderer?.setShowHint(sh) }
            if let sp = params["speed"] as? NSNumber { renderer?.setSpeed(sp.floatValue) }
            if let alpha = params["bodyAlpha"] as? NSNumber { renderer?.setBodyAlpha(alpha.floatValue) }
            if let fc = params["faceColors"] as? [NSNumber] { renderer?.setFaceColors(fc) }
            if let cam = params["cameraPosition"] as? [String: NSNumber] {
                renderer?.setCameraPositionLat(cam["latitude"]?.floatValue ?? 35, lon: cam["longitude"]?.floatValue ?? 45, rad: cam["radius"]?.floatValue ?? 6.0)
            }
            if let initialAlg = params["initialAlgorithm"] as? String, !initialAlg.isEmpty {
                renderer?.applyAlgorithm(initialAlg)
            }
            if let te = params["touchEnabled"] as? Bool {
                touchEnabled = te
            }
        }
        
        setupGestures()
        
        lastTime = CACurrentMediaTime()
        displayLink = CADisplayLink(target: self, selector: #selector(renderFrame))
        displayLink?.add(to: .current, forMode: .common)
        
        channel.setMethodCallHandler({ [weak self] (call, result) in
            self?.handle(call, result: result)
        })
    }
    
    func setupGestures() {
        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        pan.delegate = self
        _view.addGestureRecognizer(pan)
        
        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        pinch.delegate = self
        _view.addGestureRecognizer(pinch)
        
        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        tap.delegate = self
        _view.addGestureRecognizer(tap)
    }
    
    @objc func handleTap(_ gesture: UITapGestureRecognizer) {
        if gesture.state == .ended {
            channel.invokeMethod("onTap", arguments: nil)
        }
    }
    
    @objc func handlePinch(_ gesture: UIPinchGestureRecognizer) {
        if !touchEnabled { return }
        
        let scaleFactor: CGFloat = UIScreen.main.nativeScale
        
        if gesture.state == .began {
            wasScaling = true
            scaleLastSpan = gesture.scale
        } else if gesture.state == .changed {
            let delta = Float(gesture.scale - scaleLastSpan) * 10.0
            renderer?.onZoom(delta)
            scaleLastSpan = gesture.scale
        } else if gesture.state == .ended || gesture.state == .cancelled {
            // we will reset drag in Pan handler if it resumes
        }
    }
    
    @objc func handlePan(_ gesture: UIPanGestureRecognizer) {
        if !touchEnabled { return }
        let scaleFactor = UIScreen.main.nativeScale
        let loc = gesture.location(in: _view)
        let x = Float(loc.x * scaleFactor)
        let y = Float(loc.y * scaleFactor)
        
        if gesture.numberOfTouches > 1 {
            wasScaling = true
            return
        }
        
        if gesture.state == .began {
            wasScaling = false
            renderer?.onDragBeginX(x, y: y)
        } else if gesture.state == .changed {
            if wasScaling {
                wasScaling = false
                renderer?.onDragBeginX(x, y: y)
            } else {
                renderer?.onDragMoveX(x, y: y)
            }
        } else if gesture.state == .ended || gesture.state == .cancelled {
            renderer?.onDragEndX()
        }
    }
    
    // Allow simultaneous gestures (pan + pinch)
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        return true
    }
    
    @objc func renderFrame() {
        let now = CACurrentMediaTime()
        let dt = Float(now - lastTime)
        lastTime = now
        renderer?.render(dt)
    }
    
    func view() -> UIView {
        return _view
    }
    
    func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "applyAlgorithm":
            if let args = call.arguments as? [String: Any], let alg = args["alg"] as? String {
                renderer?.applyAlgorithm(alg)
            }
            result(nil)
        case "reset":
            renderer?.reset()
            result(nil)
        case "play":
            renderer?.play()
            result(nil)
        case "pause":
            renderer?.pause()
            result(nil)
        case "stepForward":
            renderer?.stepForward()
            result(nil)
        case "stepBackward":
            renderer?.stepBackward()
            result(nil)
        case "seekFraction":
            if let args = call.arguments as? [String: Any], let f = args["fraction"] as? NSNumber {
                renderer?.seekFraction(f.floatValue)
            }
            result(nil)
        case "setSpeed":
            if let args = call.arguments as? [String: Any], let s = args["speed"] as? NSNumber {
                renderer?.setSpeed(s.floatValue)
            }
            result(nil)
        case "setPitchLock":
            if let args = call.arguments as? [String: Any], let locked = args["locked"] as? Bool {
                renderer?.setPitchLock(locked)
            }
            result(nil)
        case "setShowHint":
            if let args = call.arguments as? [String: Any], let show = args["enabled"] as? Bool {
                renderer?.setShowHint(show)
            }
            result(nil)
        case "setDebugLogs":
            if let args = call.arguments as? [String: Any], let enabled = args["enabled"] as? Bool {
                renderer?.setDebugLogs(enabled)
            }
            result(nil)
        case "isPlaying":
            result(renderer?.isPlaying() ?? false)
        case "isAnimating":
            result(renderer?.isAnimating() ?? false)
        case "currentFraction":
            result(renderer?.currentFraction() ?? 0.0)
        default:
            result(FlutterMethodNotImplemented)
        }
    }
    
    deinit {
        displayLink?.invalidate()
        renderer?.destroy()
    }
}
