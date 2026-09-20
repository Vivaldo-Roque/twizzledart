#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint twizzledart.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'twizzledart'
  s.version          = '0.0.1'
  s.summary          = 'A new Flutter plugin project.'
  s.description      = <<-DESC
A new Flutter plugin project.
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }

  s.source           = { :path => '.' }

  # If your plugin requires a privacy manifest, for example if it collects user
  # data, update the PrivacyInfo.xcprivacy file to describe your plugin's
  # privacy impact, and then uncomment this line. For more information,
  # see https://developer.apple.com/documentation/bundleresources/privacy_manifest_files
  # s.resource_bundles = {'twizzledart_privacy' => ['twizzledart/Sources/twizzledart/PrivacyInfo.xcprivacy']}

  s.prepare_command = <<-CMD
    if [ ! -d "glm" ]; then
      git clone --depth 1 --branch 1.0.1 https://github.com/g-truc/glm.git glm
    fi

    WGPU_VERSION="v22.1.0.5"
    if [ ! -f "wgpu-native/lib/libwgpu_native.a" ]; then
      mkdir -p wgpu-native
      # macOS arm64/x86_64 universal binary if possible, but wgpu-native provides macos-aarch64 and macos-x86_64 separately.
      # For simplicity in this podspec, we will download macos-aarch64 (Apple Silicon).
      curl -L "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-macos-aarch64-release.zip" -o wgpu-native.zip
      unzip -o wgpu-native.zip -d wgpu-native
      rm wgpu-native.zip
    fi
  CMD

  s.source_files = 'twizzledart/Sources/twizzledart/**/*', '../native/src/**/*.cpp'
  s.public_header_files = 'twizzledart/Sources/twizzledart/**/*.h'
  s.vendored_libraries = 'wgpu-native/lib/libwgpu_native.a'
  
  s.frameworks = 'Metal', 'QuartzCore', 'Foundation', 'AppKit'

  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.14'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'HEADER_SEARCH_PATHS' => '"${PODS_TARGET_SRCROOT}/../native/include" "${PODS_TARGET_SRCROOT}/glm" "${PODS_TARGET_SRCROOT}/wgpu-native/include" "${PODS_TARGET_SRCROOT}/wgpu-native/include/webgpu"'
  }
  s.swift_version = '5.0'
end
