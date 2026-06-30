import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'twizzledart_platform_interface.dart';

/// An implementation of [TwizzledartPlatform] that uses method channels.
class MethodChannelTwizzledart extends TwizzledartPlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('twizzledart');

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>(
      'getPlatformVersion',
    );
    return version;
  }
}
