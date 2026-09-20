import 'dart:async';
import 'package:flutter/material.dart';
import 'package:twizzledart/twizzledart.dart';

import 'anim_cube_app_bar.dart';
import 'anim_cube_controls.dart';

class AnimCube extends StatefulWidget {
  final String moves;
  final String alg;

  const AnimCube({super.key, required this.moves, required this.alg});

  @override
  State<AnimCube> createState() => _AnimCubeState();
}

/// Dart Color order (U,D,F,B,L,R) -> C++ face order (+X=R,-X=L,+Y=U,-Y=D,+Z=F,-Z=B)
/// Returns 18 doubles: Rr,Rg,Rb, Lr,Lg,Lb, Ur,Ug,Ub, Dr,Dg,Db, Fr,Fg,Fb, Br,Bg,Bb.
List<double> _colorListToFaceColors(List<Color> colors) {
  final faceOrder = [5, 4, 0, 1, 2, 3]; // index into colors[]
  return [
    for (final idx in faceOrder) ...[
      colors[idx].r,
      colors[idx].g,
      colors[idx].b,
    ]
  ];
}

class _AnimCubeState extends State<AnimCube> {
  TwizzleViewController? _controller;
  bool _isPlaying = false;
  bool _showHints = true;
  double _currentSpeed = 0.3;
  bool _initialized = false;

  // Progress & Controls state
  Timer? _progressTimer;
  double _sliderValue = 0.0;
  double _startFraction = 0.0;
  final bool _touchEnabled = true;
  bool _pitchLock = false; // Novo estado para controlar a trava
  final double _zoomRadius = 10.0; // Increased radius to zoom out initially
  final double _cameraLatitude = 35.0;
  final double _cameraLongitude = 30.0;

  final List<Color> defaultColors = [
    const Color(0xFFFFFFFF), // U
    const Color(0xFFFFFF00), // D
    const Color(0xFF00FF00), // F
    const Color(0xFF0000FF), // B
    const Color(0xFFFF8800), // L
    const Color(0xFFFF0000), // R
  ];

  @override
  void dispose() {
    _progressTimer?.cancel();
    super.dispose();
  }

  int _countMoves(String algStr) {
    String cleaned = '';
    bool inComment = false;
    for (int i = 0; i < algStr.length; ++i) {
      if (!inComment &&
          i + 1 < algStr.length &&
          algStr[i] == '/' &&
          algStr[i + 1] == '/') {
        inComment = true;
      }
      if (algStr[i] == '\n') inComment = false;
      if (!inComment) cleaned += algStr[i];
    }

    String tmp = '';
    bool inBlock = false;
    for (int i = 0; i < cleaned.length; ++i) {
      if (!inBlock &&
          i + 1 < cleaned.length &&
          cleaned[i] == '(' &&
          cleaned[i + 1] == '*') {
        inBlock = true;
        i++;
        continue;
      }
      if (inBlock &&
          i + 1 < cleaned.length &&
          cleaned[i] == '*' &&
          cleaned[i + 1] == ')') {
        inBlock = false;
        i++;
        continue;
      }
      if (!inBlock) tmp += cleaned[i];
    }
    cleaned = tmp;

    cleaned = cleaned.replaceAll(RegExp(r'[()\[\],:\n\t]'), ' ');

    List<String> tokens =
        cleaned.split(RegExp(r'\s+')).where((t) => t.isNotEmpty).toList();

    int count = 0;
    final validFaces = RegExp(r'^[rludfbmesxyz]', caseSensitive: false);
    for (var token in tokens) {
      if (validFaces.hasMatch(token)) {
        count++;
      }
    }
    return count;
  }

  void _startProgressTimer() {
    _progressTimer?.cancel();
    _progressTimer =
        Timer.periodic(const Duration(milliseconds: 30), (timer) async {
      if (_controller != null) {
        final fraction = await _controller!.getCurrentFraction();
        final playing = await _controller!.isPlaying();
        if (mounted) {
          setState(() {
            if (_startFraction < 1.0) {
              double val = (fraction - _startFraction) / (1.0 - _startFraction);
              _sliderValue = val.clamp(0.0, 1.0);
            } else {
              _sliderValue = 0.0;
            }
            _isPlaying = playing;
          });
        }
        if (!playing) {
          _progressTimer?.cancel();
        }
      }
    });
  }

  void _stopProgressTimer() {
    _progressTimer?.cancel();
  }

  Future<void> _initCube(TwizzleViewController controller) async {
    _controller = controller;

    await controller.setFaceColors(_colorListToFaceColors(defaultColors));
    await controller.setAppearance(TwizzleAppearance.crystal);
    await controller.setShowHint(_showHints);

    await controller.setSpeed(100.0);
    await controller.applyAlgorithm(widget.moves);
    await Future.delayed(const Duration(milliseconds: 100));

    await controller.setSpeed(_currentSpeed);
    await controller.applyAlgorithm(widget.alg);
    await controller.pause();

    int setupMovesCount = _countMoves(widget.moves);
    int algMovesCount = _countMoves(widget.alg);
    int totalMovesCount = setupMovesCount + algMovesCount;
    _startFraction =
        totalMovesCount > 0 ? setupMovesCount / totalMovesCount : 0.0;

    debugPrint('🎬 Twizzle AnimCube Initialized');

    await controller.seekFraction(_startFraction);

    if (mounted) {
      setState(() {
        _initialized = true;
        _sliderValue = 0.0;
      });
    }
  }

  Future<void> _togglePlay() async {
    if (_controller == null) return;
    if (_isPlaying) {
      await _controller!.pause();
      _stopProgressTimer();
    } else {
      final fraction = await _controller!.getCurrentFraction();
      if (fraction >= 0.99) {
        await _controller!.seekFraction(_startFraction);
      }
      await _controller!.play();
      _startProgressTimer();
    }
    final playing = await _controller!.isPlaying();
    setState(() {
      _isPlaying = playing;
    });
  }

  Future<void> _stepForward() async {
    if (_controller == null) return;
    await _controller!.stepForward();
    _stopProgressTimer();
    final fraction = await _controller!.getCurrentFraction();
    setState(() {
      _isPlaying = false;
      if (_startFraction < 1.0) {
        _sliderValue = ((fraction - _startFraction) / (1.0 - _startFraction))
            .clamp(0.0, 1.0);
      } else {
        _sliderValue = 0.0;
      }
    });
  }

  Future<void> _stepBackward() async {
    if (_controller == null) return;

    final fraction = await _controller!.getCurrentFraction();
    if (fraction <= _startFraction + 0.01) {
      await _controller!.seekFraction(_startFraction);
      setState(() {
        _isPlaying = false;
        _showHints = true;
        _sliderValue = 0.0;
      });
      return;
    }

    await _controller!.stepBackward();
    _stopProgressTimer();
    final nextFraction = await _controller!.getCurrentFraction();
    setState(() {
      _isPlaying = false;
      if (nextFraction <= _startFraction) {
        _sliderValue = 0.0;
      } else if (_startFraction < 1.0) {
        _sliderValue =
            ((nextFraction - _startFraction) / (1.0 - _startFraction))
                .clamp(0.0, 1.0);
      } else {
        _sliderValue = 0.0;
      }
    });
  }

  Future<void> _reset() async {
    if (_controller == null) return;
    _stopProgressTimer();
    await _controller!.seekFraction(_startFraction);
    setState(() {
      _isPlaying = false;
      _sliderValue = 0.0;
    });
  }

  Future<void> _skipToEnd() async {
    if (_controller == null) return;
    _stopProgressTimer();
    await _controller!.seekFraction(1.0);
    setState(() {
      _isPlaying = false;
      _sliderValue = 1.0;
    });
  }

  Future<void> _seekFraction(double val) async {
    if (_controller == null) return;
    _stopProgressTimer();
    double nativeFraction = _startFraction + val * (1.0 - _startFraction);
    await _controller!.seekFraction(nativeFraction);
    setState(() {
      _isPlaying = false;
      _sliderValue = val;
    });
  }

  Future<void> _changeSpeed(double speed) async {
    if (_controller == null) return;
    await _controller!.setSpeed(speed);
    setState(() {
      _currentSpeed = speed;
    });
  }

  Future<void> _toggleHints() async {
    if (_controller == null) return;
    bool newShow = !_showHints;
    
    // Atualiza a UI e o estado imediatamente para evitar 'race conditions' 
    // se o usuário clicar várias vezes rápido.
    setState(() {
      _showHints = newShow;
    });
    
    await _controller!.setShowHint(newShow);
  }

  Future<void> _togglePitchLock() async {
    if (_controller == null) return;
    bool newLock = !_pitchLock;
    
    setState(() {
      _pitchLock = newLock;
    });
    
    await _controller!.setPitchLock(newLock);
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    // Use a slightly darker background (e.g. surfaceContainerHighest or a light gray)
    // so that the white faces of the cube stand out in light mode.
    final baseBg = theme.colorScheme.surface;
    final bgColor = theme.brightness == Brightness.light 
        ? const Color(0xFFE8E9EB) // Light grayish-white
        : baseBg;

    final bgConfig = {
      'r': (bgColor.r * 255.0).round().clamp(0, 255) / 255.0,
      'g': (bgColor.g * 255.0).round().clamp(0, 255) / 255.0,
      'b': (bgColor.b * 255.0).round().clamp(0, 255) / 255.0,
      'a': (bgColor.a * 255.0).round().clamp(0, 255) / 255.0,
    };

    return Scaffold(
      appBar: const AnimCubeAppBar(),
      body: SafeArea(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.start,
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            Expanded(
              child: Stack(
                alignment: AlignmentDirectional.center,
                children: [
                  TwizzleView(
                    speed: _currentSpeed,
                    initialAlgorithm: "",
                    backgroundColor: bgConfig,
                    faceColors: _colorListToFaceColors(defaultColors),
                    touchEnabled: _touchEnabled,
                    pitchLock: _pitchLock,
                    debugLogs: false, // Oculta logs do C++
                    cameraPosition: {
                      'latitude': _cameraLatitude,
                      'longitude': _cameraLongitude,
                      'radius': _zoomRadius,
                    },
                    onTap: () {},
                    onViewCreated: _initCube,
                  ),
                  if (!_initialized)
                    Container(
                      color: Theme.of(context).scaffoldBackgroundColor,
                      height: double.infinity,
                      width: double.infinity,
                      alignment: Alignment.center,
                      child: const SizedBox(
                        height: 300,
                        width: 300,
                        child: Padding(
                          padding: EdgeInsets.all(20),
                          child: CircularProgressIndicator(),
                        ),
                      ),
                    ),
                  Positioned(
                    top: 8,
                    right: 8,
                    child: Row(
                      children: [
                        IconButton(
                          icon: Icon(
                            _pitchLock ? Icons.lock : Icons.lock_open,
                            color: _pitchLock ? Colors.red : Colors.green,
                          ),
                          onPressed: _initialized ? _togglePitchLock : null,
                          tooltip: "Toggle Pitch Lock",
                        ),
                        IconButton(
                          icon: Icon(
                            _showHints ? Icons.visibility : Icons.visibility_off,
                            color: _showHints ? Colors.blue : Colors.grey,
                          ),
                          onPressed: _initialized ? _toggleHints : null,
                          tooltip: "Toggle Hints",
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
            AnimCubeControls(
              sliderValue: _sliderValue,
              initialized: _initialized,
              isPlaying: _isPlaying,
              currentSpeed: _currentSpeed,
              onSeek: _seekFraction,
              onReset: _reset,
              onStepBackward: _stepBackward,
              onTogglePlay: _togglePlay,
              onStepForward: _stepForward,
              onSkipToEnd: _skipToEnd,
              onChangeSpeed: _changeSpeed,
            ),
          ],
        ),
      ),
    );
  }
}
