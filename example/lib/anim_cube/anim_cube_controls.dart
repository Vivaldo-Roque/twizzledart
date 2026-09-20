import 'package:flutter/material.dart';

class AnimCubeControls extends StatelessWidget {
  final double sliderValue;
  final bool initialized;
  final bool isPlaying;
  final double currentSpeed;

  final ValueChanged<double>? onSeek;
  final VoidCallback? onReset;
  final VoidCallback? onStepBackward;
  final VoidCallback? onTogglePlay;
  final VoidCallback? onStepForward;
  final VoidCallback? onSkipToEnd;
  final ValueChanged<double>? onChangeSpeed;

  const AnimCubeControls({
    super.key,
    required this.sliderValue,
    required this.initialized,
    required this.isPlaying,
    required this.currentSpeed,
    this.onSeek,
    this.onReset,
    this.onStepBackward,
    this.onTogglePlay,
    this.onStepForward,
    this.onSkipToEnd,
    this.onChangeSpeed,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Container(
      padding: const EdgeInsets.only(
          top: 24.0, bottom: 48.0, left: 24.0, right: 24.0),
      decoration: BoxDecoration(
        color: theme.colorScheme.surfaceContainerHigh,
        borderRadius: const BorderRadius.only(
          topLeft: Radius.circular(32.0),
          topRight: Radius.circular(32.0),
        ),
        border: Border(
          top: BorderSide(color: theme.dividerColor.withValues(alpha: 0.1)),
        ),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          // Progress Slider Timeline
          Row(
            children: [
              Text(
                "Progresso",
                style: theme.textTheme.titleMedium?.copyWith(
                  color: theme.colorScheme.onSurfaceVariant,
                  fontWeight: FontWeight.w600,
                ),
              ),
              const SizedBox(width: 16.0),
              Expanded(
                child: Slider(
                  value: sliderValue,
                  min: 0.0,
                  max: 1.0,
                  activeColor: theme.colorScheme.primary,
                  inactiveColor: theme.colorScheme.primaryContainer,
                  onChanged: initialized ? onSeek : null,
                ),
              ),
            ],
          ),
          const SizedBox(height: 16.0),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceEvenly,
            children: [
              // Restart
              IconButton(
                iconSize: 32.0,
                icon: const Icon(Icons.replay),
                onPressed: initialized ? onReset : null,
                tooltip: 'Restart',
              ),
              // Step Backward
              IconButton(
                iconSize: 36.0,
                icon: const Icon(Icons.skip_previous),
                onPressed: initialized ? onStepBackward : null,
                tooltip: 'Step Back',
              ),
              // Play / Pause
              Material(
                color: initialized
                    ? theme.colorScheme.primaryContainer
                    : theme.disabledColor.withValues(alpha: 0.1),
                shape: const CircleBorder(),
                clipBehavior: Clip.antiAlias,
                child: InkWell(
                  onTap: initialized ? onTogglePlay : null,
                  child: SizedBox(
                    height: 80.0,
                    width: 80.0,
                    child: Icon(
                      isPlaying ? Icons.pause : Icons.play_arrow,
                      size: 48.0,
                      color: initialized
                          ? theme.colorScheme.onPrimaryContainer
                          : theme.disabledColor,
                    ),
                  ),
                ),
              ),
              // Step Forward
              IconButton(
                iconSize: 36.0,
                icon: const Icon(Icons.skip_next),
                onPressed: initialized ? onStepForward : null,
                tooltip: 'Step Forward',
              ),
              // Skip to End
              IconButton(
                iconSize: 32.0,
                icon: const Icon(Icons.fast_forward),
                onPressed: initialized ? onSkipToEnd : null,
                tooltip: 'Skip to End',
              ),
            ],
          ),
          const SizedBox(height: 24.0),
          // Velocity / Speed Slider
          Row(
            children: [
              Icon(Icons.speed,
                  size: 24.0, color: theme.colorScheme.onSurfaceVariant),
              const SizedBox(width: 12.0),
              SizedBox(
                width: 140.0,
                child: Text(
                  "Velocidade: ${currentSpeed.toStringAsFixed(1)}x",
                  style: theme.textTheme.titleMedium?.copyWith(
                    color: theme.colorScheme.onSurfaceVariant,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
              Expanded(
                child: Slider(
                  value: currentSpeed,
                  min: 0.1,
                  max: 1.0,
                  divisions: 9,
                  activeColor: theme.colorScheme.primary,
                  inactiveColor: theme.colorScheme.primaryContainer,
                  onChanged: initialized ? onChangeSpeed : null,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
