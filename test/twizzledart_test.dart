import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:twizzledart/twizzledart.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('TwizzleAppearance Enum', () {
    test('contains crystal and normal modes', () {
      expect(TwizzleAppearance.values, contains(TwizzleAppearance.crystal));
      expect(TwizzleAppearance.values, contains(TwizzleAppearance.normal));
      expect(TwizzleAppearance.values.length, 2);
    });
  });

  group('TwizzleView - Fallback Rendering on Unsupported Platform', () {
    testWidgets('renders unsupported platform message on non-Android platform', (
      tester,
    ) async {
      debugDefaultTargetPlatformOverride = TargetPlatform.windows;

      try {
        TwizzleViewController? createdController;

        final widget = TwizzleView(
          initialAlgorithm: "R U R' U'",
          speed: 1.5,
          touchEnabled: true,
          appearance: TwizzleAppearance.crystal,
          onViewCreated: (controller) {
            createdController = controller;
          },
        );

        await tester.pumpWidget(
          Directionality(
            textDirection: TextDirection.ltr,
            child: widget,
          ),
        );

        expect(
          find.textContaining('is not supported by TwizzleView yet'),
          findsOneWidget,
        );
        expect(createdController, isNull);
      } finally {
        debugDefaultTargetPlatformOverride = null;
      }
    });
  });

  group('TwizzleViewController - Direct Method Testing', () {
    const int viewId = 99;
    const String channelName = 'twizzledart_view_$viewId';
    final List<MethodCall> log = [];

    setUp(() {
      log.clear();
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(const MethodChannel(channelName), (
            MethodCall call,
          ) async {
            log.add(call);
            switch (call.method) {
              case 'getSpeed':
                return 2.5;
              case 'isAnimating':
                return true;
              case 'isPlaying':
                return true;
              case 'getCurrentFraction':
                return 0.42;
              default:
                return null;
            }
          });
    });

    tearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(const MethodChannel(channelName), null);
    });

    testWidgets('exercises all controller methods and appearance modes', (
      tester,
    ) async {
      debugDefaultTargetPlatformOverride = TargetPlatform.android;

      try {
        TwizzleViewController? capturedController;
        var tapCount = 0;

        await tester.pumpWidget(
          Directionality(
            textDirection: TextDirection.ltr,
            child: TwizzleView(
              initialAlgorithm: "F R U R' U' F'",
              speed: 1.0,
              touchEnabled: true,
              appearance: TwizzleAppearance.crystal,
              onViewCreated: (ctrl) {
                capturedController = ctrl;
              },
              onTap: () {
                tapCount++;
              },
            ),
          ),
        );

        // Find the AndroidView and invoke onPlatformViewCreated
        final androidViewFinder = find.byType(AndroidView);
        expect(androidViewFinder, findsOneWidget);

        final AndroidView androidView = tester.widget(androidViewFinder);
        androidView.onPlatformViewCreated!(viewId);

        expect(capturedController, isNotNull);
        final controller = capturedController!;

        // 1. applyAlgorithm
        await controller.applyAlgorithm("R U R' U'");
        expect(log.last.method, 'applyAlgorithm');
        expect(log.last.arguments, {'algorithm': "R U R' U'"});

        // 2. reset
        await controller.reset();
        expect(log.last.method, 'reset');

        // 3. setSpeed & getSpeed
        await controller.setSpeed(2.0);
        expect(log.last.method, 'setSpeed');
        expect(log.last.arguments, {'speed': 2.0});

        final speed = await controller.getSpeed();
        expect(speed, 2.5);

        // 4. isAnimating
        final animating = await controller.isAnimating();
        expect(animating, isTrue);

        // 5. play & pause
        await controller.play();
        expect(log.last.method, 'play');

        await controller.pause();
        expect(log.last.method, 'pause');

        // 6. stepForward & stepBackward
        await controller.stepForward();
        expect(log.last.method, 'stepForward');

        await controller.stepBackward();
        expect(log.last.method, 'stepBackward');

        // 7. isPlaying
        final playing = await controller.isPlaying();
        expect(playing, isTrue);

        // 8. setBackgroundColor
        await controller.setBackgroundColor(0.1, 0.2, 0.3, 1.0);
        expect(log.last.method, 'setBackgroundColor');
        expect(log.last.arguments, {'r': 0.1, 'g': 0.2, 'b': 0.3, 'a': 1.0});

        // 9. setCameraPosition
        await controller.setCameraPosition(45.0, 90.0, 5.0);
        expect(log.last.method, 'setCameraPosition');
        expect(log.last.arguments, {
          'latitude': 45.0,
          'longitude': 90.0,
          'radius': 5.0,
        });

        // 10. seekFraction & getCurrentFraction
        await controller.seekFraction(0.65);
        expect(log.last.method, 'seekFraction');
        expect(log.last.arguments, {'fraction': 0.65});

        final frac = await controller.getCurrentFraction();
        expect(frac, 0.42);

        // 11. setTouchEnabled
        await controller.setTouchEnabled(false);
        expect(log.last.method, 'setTouchEnabled');
        expect(log.last.arguments, {'enabled': false});

        // 12. setShowHint
        await controller.setShowHint(true);
        expect(log.last.method, 'setShowHint');
        expect(log.last.arguments, {'enabled': true});

        // 13. setFaceColors
        final colors18 = List<double>.generate(18, (i) => i * 0.05);
        await controller.setFaceColors(colors18);
        expect(log.last.method, 'setFaceColors');
        expect(log.last.arguments, {'colors': colors18});

        expect(
          () => controller.setFaceColors([1.0, 2.0]),
          throwsA(isA<AssertionError>()),
        );

        // 14. setBodyAlpha
        await controller.setBodyAlpha(0.5);
        expect(log.last.method, 'setBodyAlpha');
        expect(log.last.arguments, {'alpha': 0.5});

        // 15. setAppearance
        await controller.setAppearance(TwizzleAppearance.crystal);
        expect(log.last.method, 'setBodyAlpha');
        expect(log.last.arguments, {'alpha': 0.3});

        await controller.setAppearance(TwizzleAppearance.normal);
        expect(log.last.method, 'setBodyAlpha');
        expect(log.last.arguments, {'alpha': 1.0});

        // 16. onTap handler via incoming platform channel call
        final ByteData? message = const StandardMethodCodec().encodeMethodCall(
          const MethodCall('onTap'),
        );
        await TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
            .handlePlatformMessage(
              channelName,
              message,
              (ByteData? reply) {},
            );

        expect(tapCount, 1);
      } finally {
        debugDefaultTargetPlatformOverride = null;
      }
    });
  });

  group('TwizzleView Widget Configuration', () {
    testWidgets('passes creation parameters correctly to AndroidView', (
      tester,
    ) async {
      debugDefaultTargetPlatformOverride = TargetPlatform.android;

      try {
        await tester.pumpWidget(
          Directionality(
            textDirection: TextDirection.ltr,
            child: TwizzleView(
              initialAlgorithm: "R U R' U'",
              speed: 2.0,
              touchEnabled: false,
              backgroundColor: const {'r': 0.1, 'g': 0.1, 'b': 0.1},
              cameraPosition: const {
                'latitude': 30.0,
                'longitude': 60.0,
                'radius': 4.0,
              },
              faceColors: List<double>.filled(18, 1.0),
              appearance: TwizzleAppearance.crystal,
            ),
          ),
        );

        final AndroidView androidView = tester.widget(find.byType(AndroidView));
        expect(androidView.viewType, 'twizzle_view');

        final params = androidView.creationParams as Map<String, dynamic>;
        expect(params['initialAlgorithm'], "R U R' U'");
        expect(params['speed'], 2.0);
        expect(params['touchEnabled'], false);
        expect(params['backgroundColor'], {'r': 0.1, 'g': 0.1, 'b': 0.1});
        expect(params['cameraPosition'], {
          'latitude': 30.0,
          'longitude': 60.0,
          'radius': 4.0,
        });
        expect(params['faceColors'], List<double>.filled(18, 1.0));
        expect(params['bodyAlpha'], 0.3); // crystal mode -> 0.3
      } finally {
        debugDefaultTargetPlatformOverride = null;
      }
    });

    testWidgets('uses raw bodyAlpha when appearance is not specified', (
      tester,
    ) async {
      debugDefaultTargetPlatformOverride = TargetPlatform.android;

      try {
        await tester.pumpWidget(
          const Directionality(
            textDirection: TextDirection.ltr,
            child: TwizzleView(
              bodyAlpha: 0.8,
            ),
          ),
        );

        final AndroidView androidView = tester.widget(find.byType(AndroidView));
        final params = androidView.creationParams as Map<String, dynamic>;
        expect(params['bodyAlpha'], 0.8);
      } finally {
        debugDefaultTargetPlatformOverride = null;
      }
    });
  });
}
