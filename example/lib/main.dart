import 'package:flutter/material.dart';
import 'package:twizzledart/twizzledart.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('TwizzleDart Example')),
        body: const Center(
          child: SizedBox(
            width: 300,
            height: 300,
            child: TwizzleView(),
          ),
        ),
      ),
    );
  }
}
