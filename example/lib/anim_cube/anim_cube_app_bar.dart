import 'package:flutter/material.dart';

class AnimCubeAppBar extends StatelessWidget implements PreferredSizeWidget {
  const AnimCubeAppBar({super.key});

  @override
  Widget build(BuildContext context) {
    return AppBar(
      backgroundColor: Colors.transparent,
      elevation: 0,
      title: const Text(
        'TwizzleDart Example',
        style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
      ),
      centerTitle: true,
      leading: Builder(
        builder: (BuildContext context) {
          final ModalRoute<Object?>? parentRoute = ModalRoute.of(context);
          final bool canPop = parentRoute?.canPop ?? false;

          if (canPop) {
            return const BackButton();
          } else {
            return const SizedBox.shrink();
          }
        },
      ),
    );
  }

  @override
  Size get preferredSize => const Size.fromHeight(kToolbarHeight);
}
