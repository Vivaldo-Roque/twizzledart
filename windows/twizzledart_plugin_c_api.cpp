#include "include/twizzledart/twizzledart_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "twizzledart_plugin.h"

void TwizzledartPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  twizzledart::TwizzledartPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
