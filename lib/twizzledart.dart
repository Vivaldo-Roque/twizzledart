import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// Callback when the 3D Twizzle view is ready to be controlled.
typedef TwizzleViewCreatedCallback = void Function(TwizzleViewController controller);

/// Visual appearance mode for the cube.
enum TwizzleAppearance {
  /// Crystal/glassy look: translucent body (alpha 0.3) + hint stickers.
  crystal,
  /// Normal look: opaque black body (alpha 1.0), no hint stickers.
  normal,
}

/// Controller to apply algorithms and manage the state of a [TwizzleView].
class TwizzleViewController {
  final int _id;
  late final MethodChannel _channel;

  /// Callback triggered when the view is tapped (only if touch interaction is disabled).
  VoidCallback? onTap;

  TwizzleViewController._(this._id) {
    _channel = MethodChannel('twizzledart_view_$_id');
    _channel.setMethodCallHandler(_handleMethodCall);
  }

  Future<void> _handleMethodCall(MethodCall call) async {
    switch (call.method) {
      case 'onTap':
        onTap?.call();
        break;
      default:
        break;
    }
  }

  /// Applies a WCA-notation algorithm to the cube view (e.g. "R U R' U'").
  Future<void> applyAlgorithm(String algorithm) async {
    try {
      await _channel.invokeMethod('applyAlgorithm', {'algorithm': algorithm});
    } on PlatformException catch (e) {
      debugPrint("Error applying algorithm: ${e.message}");
    }
  }

  /// Resets the cube to its solved state.
  Future<void> reset() async {
    try {
      await _channel.invokeMethod('reset');
    } on PlatformException catch (e) {
      debugPrint("Error resetting cube: ${e.message}");
    }
  }

  /// Sets the animation speed of the cube moves (e.g. 1.0 = normal, 2.0 = double, 0.5 = half).
  Future<void> setSpeed(double speed) async {
    try {
      await _channel.invokeMethod('setSpeed', {'speed': speed});
    } on PlatformException catch (e) {
      debugPrint("Error setting speed: ${e.message}");
    }
  }

  /// Gets the current animation speed.
  Future<double> getSpeed() async {
    try {
      final double? speed = await _channel.invokeMethod<double>('getSpeed');
      return speed ?? 1.0;
    } on PlatformException catch (e) {
      debugPrint("Error getting speed: ${e.message}");
      return 1.0;
    }
  }

  /// Checks if the cube is currently performing a move animation.
  Future<bool> isAnimating() async {
    try {
      final bool? animating = await _channel.invokeMethod<bool>('isAnimating');
      return animating ?? false;
    } on PlatformException catch (e) {
      debugPrint("Error checking animation state: ${e.message}");
      return false;
    }
  }

  /// Starts or resumes move animation.
  Future<void> play() async {
    try {
      await _channel.invokeMethod('play');
    } on PlatformException catch (e) {
      debugPrint("Error playing animation: ${e.message}");
    }
  }

  /// Pauses move animation.
  Future<void> pause() async {
    try {
      await _channel.invokeMethod('pause');
    } on PlatformException catch (e) {
      debugPrint("Error pausing animation: ${e.message}");
    }
  }

  /// Steps one move forward in the animation.
  Future<void> stepForward() async {
    try {
      await _channel.invokeMethod('stepForward');
    } on PlatformException catch (e) {
      debugPrint("Error stepping forward: ${e.message}");
    }
  }

  /// Steps one move backward in the animation.
  Future<void> stepBackward() async {
    try {
      await _channel.invokeMethod('stepBackward');
    } on PlatformException catch (e) {
      debugPrint("Error stepping backward: ${e.message}");
    }
  }

  /// Checks if the animation is currently playing (active running timeline).
  Future<bool> isPlaying() async {
    try {
      final bool? playing = await _channel.invokeMethod<bool>('isPlaying');
      return playing ?? false;
    } on PlatformException catch (e) {
      debugPrint("Error checking playing state: ${e.message}");
      return false;
    }
  }

  /// Sets the viewport background color.
  Future<void> setBackgroundColor(double r, double g, double b, double a) async {
    try {
      await _channel.invokeMethod('setBackgroundColor', {'r': r, 'g': g, 'b': b, 'a': a});
    } on PlatformException catch (e) {
      debugPrint("Error setting background color: ${e.message}");
    }
  }

  /// Sets the OrbitCamera position (latitude, longitude in degrees; radius for zoom).
  Future<void> setCameraPosition(double latitude, double longitude, double radius) async {
    try {
      await _channel.invokeMethod('setCameraPosition', {
        'latitude': latitude,
        'longitude': longitude,
        'radius': radius,
      });
    } on PlatformException catch (e) {
      debugPrint("Error setting camera position: ${e.message}");
    }
  }

  /// Seeks to a fraction of the full timeline in [0,1].
  Future<void> seekFraction(double fraction) async {
    try {
      await _channel.invokeMethod('seekFraction', {'fraction': fraction});
    } on PlatformException catch (e) {
      debugPrint("Error seeking fraction: ${e.message}");
    }
  }

  /// Gets the current fraction of the animation timeline.
  Future<double> getCurrentFraction() async {
    try {
      final double? fraction = await _channel.invokeMethod<double>('getCurrentFraction');
      return fraction ?? 0.0;
    } on PlatformException catch (e) {
      debugPrint("Error getting current fraction: ${e.message}");
      return 0.0;
    }
  }

  /// Enables or disables direct touch gestures (orbit drag, pinch zoom) on the cube.
  Future<void> setTouchEnabled(bool enabled) async {
    try {
      await _channel.invokeMethod('setTouchEnabled', {'enabled': enabled});
    } on PlatformException catch (e) {
      debugPrint("Error setting touch enabled: ${e.message}");
    }
  }

  /// Sets whether the camera pitch is locked to [-89, 89] degrees.
  Future<void> setPitchLock(bool locked) async {
    try {
      await _channel.invokeMethod('setPitchLock', {'locked': locked});
    } on PlatformException catch (e) {
      debugPrint("Error setting pitch lock: ${e.message}");
    }
  }

  /// Enables or disables native debug logs.
  Future<void> setDebugLogs(bool enabled) async {
    try {
      await _channel.invokeMethod('setDebugLogs', {'enabled': enabled});
    } on PlatformException catch (e) {
      debugPrint("Error setting debug logs: ${e.message}");
    }
  }

  /// Shows or hides the hint stickers.
  Future<void> setShowHint(bool enabled) async {
    try {
      await _channel.invokeMethod('setShowHint', {'enabled': enabled});
    } on PlatformException catch (e) {
      debugPrint("Error setting show hint: ${e.message}");
    }
  }

  /// Sets sticker colours for all 6 faces (18 doubles: R,G,B per face).
  Future<void> setFaceColors(List<double> colors) async {
    assert(colors.length == 18);
    try {
      await _channel.invokeMethod('setFaceColors', {'colors': colors});
    } on PlatformException catch (e) {
      debugPrint("Error setting face colors: ${e.message}");
    }
  }

  /// Sets the cubie body (foundation) opacity. 0.3 = crystal, 1.0 = normal.
  Future<void> setBodyAlpha(double alpha) async {
    try {
      await _channel.invokeMethod('setBodyAlpha', {'alpha': alpha});
    } on PlatformException catch (e) {
      debugPrint("Error setting body alpha: ${e.message}");
    }
  }

  /// Convenience: applies a pre-defined body appearance to the cube.
  Future<void> setAppearance(TwizzleAppearance mode) async {
    await setBodyAlpha(mode == TwizzleAppearance.crystal ? 0.3 : 1.0);
  }

  // ── Desktop drag forwarding ───────────────────────────────────────────────

  Future<void> _onDragBegin(double x, double y) async {
    try { await _channel.invokeMethod('onDragBegin', {'x': x, 'y': y}); }
    catch (_) {}
  }

  Future<void> _onDragMove(double x, double y) async {
    try { await _channel.invokeMethod('onDragMove', {'x': x, 'y': y}); }
    catch (_) {}
  }

  Future<void> _onDragEnd() async {
    try { await _channel.invokeMethod('onDragEnd'); }
    catch (_) {}
  }

}

// ---------------------------------------------------------------------------
// _TwizzleWindowsView — Windows implementation using Flutter Texture API
// ---------------------------------------------------------------------------
class _TwizzleWindowsView extends StatefulWidget {
  final String? initialAlgorithm;
  final double speed;
  final Map<String, double>? backgroundColor;
  final Map<String, double>? cameraPosition;
  final bool touchEnabled;
  final bool pitchLock;
  final bool debugLogs;
  final List<double>? faceColors;
  final TwizzleAppearance? appearance;
  final double? bodyAlpha;
  final TwizzleViewCreatedCallback? onViewCreated;
  final VoidCallback? onTap;

  const _TwizzleWindowsView({
    this.initialAlgorithm,
    this.speed = 1.0,
    this.backgroundColor,
    this.cameraPosition,
    this.touchEnabled = true,
    this.pitchLock = true,
    this.debugLogs = false,
    this.faceColors,
    this.appearance,
    this.bodyAlpha,
    this.onViewCreated,
    this.onTap,
  });

  @override
  State<_TwizzleWindowsView> createState() => _TwizzleWindowsViewState();
}

class _TwizzleWindowsViewState extends State<_TwizzleWindowsView> {
  static const MethodChannel _mainChannel = MethodChannel('twizzledart');

  int? _textureId;
  TwizzleViewController? _controller;

  @override
  void initState() {
    super.initState();
    _createView();
  }

  Future<void> _createView() async {
    final params = <String, dynamic>{
      if (widget.initialAlgorithm != null) 'initialAlgorithm': widget.initialAlgorithm,
      'speed': widget.speed,
      'touchEnabled': widget.touchEnabled,
      'pitchLock': widget.pitchLock,
      'debugLogs': widget.debugLogs,
      if (widget.backgroundColor != null) 'backgroundColor': widget.backgroundColor,
      if (widget.cameraPosition != null)  'cameraPosition':  widget.cameraPosition,
      if (widget.faceColors != null)      'faceColors':      widget.faceColors,
      if (widget.appearance != null)
        'bodyAlpha': widget.appearance == TwizzleAppearance.crystal ? 0.3 : 1.0
      else if (widget.bodyAlpha != null)
        'bodyAlpha': widget.bodyAlpha,
    };

    try {
      final int? texId = await _mainChannel.invokeMethod<int>('createView', params);
      if (!mounted || texId == null || texId < 0) return;

      final controller = TwizzleViewController._(texId);
      if (widget.onTap != null) controller.onTap = widget.onTap;

      setState(() {
        _textureId = texId;
        _controller = controller;
      });

      widget.onViewCreated?.call(controller);
    } on PlatformException catch (e) {
      debugPrint('[TwizzleView] createView failed: ${e.message}');
    }
  }

  @override
  void dispose() {
    if (_textureId != null) {
      _mainChannel.invokeMethod('disposeView', {'textureId': _textureId}).ignore();
    }
    super.dispose();
  }

  void _handleDragStart(DragStartDetails d) =>
      _controller?._onDragBegin(d.localPosition.dx, d.localPosition.dy);

  void _handleDragUpdate(DragUpdateDetails d) =>
      _controller?._onDragMove(d.localPosition.dx, d.localPosition.dy);

  void _handleDragEnd(DragEndDetails d) => _controller?._onDragEnd();

  @override
  Widget build(BuildContext context) {
    if (_textureId == null) {
      return const SizedBox.shrink();
    }

    Widget view = Texture(textureId: _textureId!);

    if (widget.touchEnabled) {
      view = GestureDetector(
        onPanStart:  _handleDragStart,
        onPanUpdate: _handleDragUpdate,
        onPanEnd:    _handleDragEnd,
        onTap:       widget.onTap != null ? widget.onTap : null,
        child: view,
      );
    } else if (widget.onTap != null) {
      view = GestureDetector(onTap: widget.onTap, child: view);
    }

    return view;
  }
}

// ---------------------------------------------------------------------------
// TwizzleView — public widget (Android + Windows)
// ---------------------------------------------------------------------------

/// A native 3D Rubik's Cube view widget.
///
/// - **Android**: OpenGL ES 3.0 via AndroidView (legacy; replaced in Phase 3).
/// - **Windows**: WebGPU (DirectX 12) via Flutter Texture API.
/// - **iOS/macOS**: coming in Phase 4 (WebGPU Metal backend).
class TwizzleView extends StatelessWidget {
  /// The initial sequence of moves (e.g., scramble or algorithm in WCA notation)
  /// applied to the cube when the view is initialized.
  final String? initialAlgorithm;

  /// The animation speed multiplier for cube rotations and move animations.
  /// Defaults to `1.0`.
  final double speed;

  /// The background color of the 3D scene, represented as a map with RGB keys.
  /// Example: `{'r': 0.1, 'g': 0.1, 'b': 0.1}`.
  final Map<String, double>? backgroundColor;

  /// Custom camera position configuration to control the viewing angle and distance.
  final Map<String, double>? cameraPosition;

  /// Whether user touch/mouse gestures to rotate the cube are enabled.
  /// Defaults to `true`.
  final bool touchEnabled;

  /// Sticker colors for all 6 faces of the cube.
  /// Must contain exactly 18 doubles (R, G, B channels for each face):
  /// `Rr, Rg, Rb, Lr, Lg, Lb, Ur, Ug, Ub, Dr, Dg, Db, Fr, Fg, Fb, Br, Bg, Bb`.
  final List<double>? faceColors;

  /// The visual appearance style of the cube body.
  ///
  /// - [TwizzleAppearance.crystal] → translucent body (bodyAlpha = 0.3)
  /// - [TwizzleAppearance.normal]  → opaque black body (bodyAlpha = 1.0)
  ///
  /// If both [appearance] and [bodyAlpha] are provided, [appearance] takes precedence.
  final TwizzleAppearance? appearance;

  /// The raw opacity of the cubie body/foundation, from `0.0` to `1.0`.
  /// Only used when [appearance] is not set.
  final double? bodyAlpha;

  /// Whether vertical rotation (pitch) is restricted to [-89, 89] degrees.
  /// Defaults to `true`.
  final bool pitchLock;

  /// Whether native debug logs are enabled.
  /// Defaults to `false`.
  final bool debugLogs;

  /// Callback triggered once the native platform view is created.
  final TwizzleViewCreatedCallback? onViewCreated;

  /// Callback triggered when the cube view is tapped.
  final VoidCallback? onTap;

  const TwizzleView({
    super.key,
    this.initialAlgorithm,
    this.speed = 1.0,
    this.backgroundColor,
    this.cameraPosition,
    this.touchEnabled = true,
    this.pitchLock = true,
    this.debugLogs = false,
    this.faceColors,
    this.appearance,
    this.bodyAlpha,
    this.onViewCreated,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    // ── Windows & macOS: WebGPU via Flutter Texture API / Platform Views ───
    if (defaultTargetPlatform == TargetPlatform.windows) {
      return _TwizzleWindowsView(
        initialAlgorithm: initialAlgorithm,
        speed: speed,
        backgroundColor: backgroundColor,
        cameraPosition: cameraPosition,
        touchEnabled: touchEnabled,
        pitchLock: pitchLock,
        debugLogs: debugLogs,
        faceColors: faceColors,
        appearance: appearance,
        bodyAlpha: bodyAlpha,
        onViewCreated: onViewCreated,
        onTap: onTap,
      );
    }

    // ── Android & iOS: Native Views (AndroidView / UiKitView) ───────────────
    if (defaultTargetPlatform == TargetPlatform.android || defaultTargetPlatform == TargetPlatform.iOS || defaultTargetPlatform == TargetPlatform.macOS) {
      const String viewType = 'twizzle_view';
      final Map<String, dynamic> creationParams = <String, dynamic>{
        if (initialAlgorithm != null) 'initialAlgorithm': initialAlgorithm,
        'speed': speed,
        'touchEnabled': touchEnabled,
        'pitchLock': pitchLock,
        'debugLogs': debugLogs,
        if (backgroundColor != null) 'backgroundColor': backgroundColor,
        if (cameraPosition != null) 'cameraPosition': cameraPosition,
        if (faceColors != null) 'faceColors': faceColors,
        if (appearance != null)
          'bodyAlpha': appearance == TwizzleAppearance.crystal ? 0.3 : 1.0
        else if (bodyAlpha != null)
          'bodyAlpha': bodyAlpha,
      };
      if (defaultTargetPlatform == TargetPlatform.android) {
        return AndroidView(
          viewType: viewType,
          onPlatformViewCreated: _onPlatformViewCreated,
          creationParams: creationParams,
          creationParamsCodec: const StandardMessageCodec(),
        );
      } else if (defaultTargetPlatform == TargetPlatform.iOS) {
        return UiKitView(
          viewType: viewType,
          onPlatformViewCreated: _onPlatformViewCreated,
          creationParams: creationParams,
          creationParamsCodec: const StandardMessageCodec(),
        );
      } else if (defaultTargetPlatform == TargetPlatform.macOS) {
        return AppKitView(
          viewType: viewType,
          onPlatformViewCreated: _onPlatformViewCreated,
          creationParams: creationParams,
          creationParamsCodec: const StandardMessageCodec(),
        );
      }
    }

    return Center(
      child: Text('$defaultTargetPlatform is not supported by TwizzleView yet.'),
    );
  }

  void _onPlatformViewCreated(int id) {
    if (onViewCreated != null) {
      final controller = TwizzleViewController._(id);
      if (onTap != null) controller.onTap = onTap;
      onViewCreated!(controller);
    }
  }
}
