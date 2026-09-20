import 'package:flutter/material.dart';
import 'anim_cube/anim_cube.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'TwizzleDart Example',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
        useMaterial3: true,
      ),
      home: const AnimCube(
        moves: "R U R' U'",
        alg: "R U R' U R U2 R'", // Sune example
      ),
    );
  }
}
