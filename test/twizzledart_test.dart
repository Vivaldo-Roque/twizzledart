import 'package:flutter_test/flutter_test.dart';
import 'package:twizzledart/twizzledart.dart';
import 'package:twizzledart/twizzledart_platform_interface.dart';
import 'package:twizzledart/twizzledart_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockTwizzledartPlatform
    with MockPlatformInterfaceMixin
    implements TwizzledartPlatform {
  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final TwizzledartPlatform initialPlatform = TwizzledartPlatform.instance;

  test('$MethodChannelTwizzledart is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelTwizzledart>());
  });

  test('getPlatformVersion', () async {
    Twizzledart twizzledartPlugin = Twizzledart();
    MockTwizzledartPlatform fakePlatform = MockTwizzledartPlatform();
    TwizzledartPlatform.instance = fakePlatform;

    expect(await twizzledartPlugin.getPlatformVersion(), '42');
  });
}
