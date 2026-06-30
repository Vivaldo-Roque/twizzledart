import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'twizzledart_method_channel.dart';

abstract class TwizzledartPlatform extends PlatformInterface {
  /// Constructs a TwizzledartPlatform.
  TwizzledartPlatform() : super(token: _token);

  static final Object _token = Object();

  static TwizzledartPlatform _instance = MethodChannelTwizzledart();

  /// The default instance of [TwizzledartPlatform] to use.
  ///
  /// Defaults to [MethodChannelTwizzledart].
  static TwizzledartPlatform get instance => _instance;

  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [TwizzledartPlatform] when
  /// they register themselves.
  static set instance(TwizzledartPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('platformVersion() has not been implemented.');
  }
}
