import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// Callback when the 3D Twizzle view is ready to be controlled.
typedef TwizzleViewCreatedCallback = void Function(TwizzleViewController controller);

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
}

/// A native 3D Rubik's Cube view widget rendered using OpenGL ES 3.0.
class TwizzleView extends StatelessWidget {
  final String? initialAlgorithm;
  final double speed;
  final Map<String, double>? backgroundColor;
  final Map<String, double>? cameraPosition;
  final bool touchEnabled;
  final TwizzleViewCreatedCallback? onViewCreated;
  final VoidCallback? onTap;

  const TwizzleView({
    super.key,
    this.initialAlgorithm,
    this.speed = 1.0,
    this.backgroundColor,
    this.cameraPosition,
    this.touchEnabled = true,
    this.onViewCreated,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    const String viewType = 'twizzle_view';
    
    final Map<String, dynamic> creationParams = <String, dynamic>{
      if (initialAlgorithm != null) 'initialAlgorithm': initialAlgorithm,
      'speed': speed,
      'touchEnabled': touchEnabled,
      if (backgroundColor != null) 'backgroundColor': backgroundColor,
      if (cameraPosition != null) 'cameraPosition': cameraPosition,
    };

    if (defaultTargetPlatform == TargetPlatform.android) {
      return AndroidView(
        viewType: viewType,
        onPlatformViewCreated: _onPlatformViewCreated,
        creationParams: creationParams,
        creationParamsCodec: const StandardMessageCodec(),
      );
    }

    return Center(
      child: Text(
        '$defaultTargetPlatform is not supported by TwizzleView yet.',
      ),
    );
  }

  void _onPlatformViewCreated(int id) {
    if (onViewCreated != null) {
      final controller = TwizzleViewController._(id);
      if (onTap != null) {
        controller.onTap = onTap;
      }
      onViewCreated!(controller);
    }
  }
}
