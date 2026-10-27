// =============================================================================
// twizzledart_plugin.cc — Linux Flutter plugin for TwizzleDart (WebGPU Vulkan)
//
// Uses FlPixelBufferTexture + FlTextureRegistrar and WebGpuRenderer in
// offscreen RGBA8 mode with event-driven on-demand rendering (wakeCv).
// =============================================================================
#include "include/twizzledart/twizzledart_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "renderer/WebGpuRenderer.hpp"

// ---------------------------------------------------------------------------
// Custom FlPixelBufferTexture GObject subclass
// ---------------------------------------------------------------------------

struct _TwizzleLinuxTexture {
  FlPixelBufferTexture parent_instance;
  std::mutex* mutex;
  uint8_t* buffer;
  uint32_t width;
  uint32_t height;
};

G_DECLARE_FINAL_TYPE(TwizzleLinuxTexture,
                     twizzle_linux_texture,
                     TWIZZLE,
                     LINUX_TEXTURE,
                     FlPixelBufferTexture)

G_DEFINE_TYPE(TwizzleLinuxTexture,
              twizzle_linux_texture,
              fl_pixel_buffer_texture_get_type())

static gboolean twizzle_linux_texture_copy_pixels(
    FlPixelBufferTexture* texture,
    const uint8_t** out_buffer,
    uint32_t* width,
    uint32_t* height,
    GError** /*error*/) {
  auto* self = TWIZZLE_LINUX_TEXTURE(texture);
  if (!self || !self->buffer || !self->mutex) return FALSE;
  std::lock_guard<std::mutex> lock(*self->mutex);
  *out_buffer = self->buffer;
  *width      = self->width;
  *height     = self->height;
  return TRUE;
}

static void twizzle_linux_texture_class_init(TwizzleLinuxTextureClass* klass) {
  FL_PIXEL_BUFFER_TEXTURE_CLASS(klass)->copy_pixels =
      twizzle_linux_texture_copy_pixels;
}

static void twizzle_linux_texture_init(TwizzleLinuxTexture* self) {
  self->mutex  = nullptr;
  self->buffer = nullptr;
  self->width  = 0;
  self->height = 0;
}

static TwizzleLinuxTexture* twizzle_linux_texture_new(
    std::mutex* mutex,
    uint8_t* buffer,
    uint32_t width,
    uint32_t height) {
  auto* tex = TWIZZLE_LINUX_TEXTURE(
      g_object_new(twizzle_linux_texture_get_type(), nullptr));
  tex->mutex  = mutex;
  tex->buffer = buffer;
  tex->width  = width;
  tex->height = height;
  return tex;
}

// ---------------------------------------------------------------------------
// Per-View Instance
// ---------------------------------------------------------------------------

struct LinuxCubeViewInstance {
  int64_t textureId = -1;
  FlTexture* flTexture = nullptr;
  FlTextureRegistrar* textureRegistrar = nullptr;
  FlMethodChannel* viewChannel = nullptr;

  std::unique_ptr<uint8_t[]> pixelBuffer;
  uint32_t bufferWidth  = 0;
  uint32_t bufferHeight = 0;

  std::unique_ptr<twizzle::renderer::WebGpuRenderer> renderer;

  std::thread             renderThread;
  std::atomic<bool>       running{false};
  std::mutex              rendererMutex;
  std::condition_variable wakeCv;
};

// ---------------------------------------------------------------------------
// FlValue helpers
// ---------------------------------------------------------------------------

static double GetFlDouble(FlValue* map, const char* key, double def = 0.0) {
  if (!map || fl_value_get_type(map) != FL_VALUE_TYPE_MAP) return def;
  FlValue* v = fl_value_lookup_string(map, key);
  if (!v) return def;
  if (fl_value_get_type(v) == FL_VALUE_TYPE_FLOAT) return fl_value_get_float(v);
  if (fl_value_get_type(v) == FL_VALUE_TYPE_INT) return (double)fl_value_get_int(v);
  return def;
}

static bool GetFlBool(FlValue* map, const char* key, bool def = false) {
  if (!map || fl_value_get_type(map) != FL_VALUE_TYPE_MAP) return def;
  FlValue* v = fl_value_lookup_string(map, key);
  if (!v || fl_value_get_type(v) != FL_VALUE_TYPE_BOOL) return def;
  return fl_value_get_bool(v);
}

static std::string GetFlString(FlValue* map, const char* key, const char* def = "") {
  if (!map || fl_value_get_type(map) != FL_VALUE_TYPE_MAP) return def;
  FlValue* v = fl_value_lookup_string(map, key);
  if (!v || fl_value_get_type(v) != FL_VALUE_TYPE_STRING) return def;
  return fl_value_get_string(v);
}

// ---------------------------------------------------------------------------
// TwizzledartPlugin GObject
// ---------------------------------------------------------------------------

struct _TwizzledartPlugin {
  GObject parent_instance;
  FlPluginRegistrar* registrar;
  FlMethodChannel* mainChannel;
  std::mutex instancesMutex;
  std::unordered_map<int64_t, std::unique_ptr<LinuxCubeViewInstance>> instances;
};

G_DEFINE_TYPE(TwizzledartPlugin, twizzledart_plugin, g_object_get_type())

static void RenderLoop(LinuxCubeViewInstance* inst) {
  using Clock = std::chrono::steady_clock;
  auto prev   = Clock::now();

  constexpr float kTargetFps = 60.0f;
  constexpr int   kFrameMs   = (int)(1000.0f / kTargetFps);

  while (inst->running) {
    bool frameRendered = false;
    bool stillNeedsRender = false;

    {
      std::unique_lock<std::mutex> lock(inst->rendererMutex);
      inst->wakeCv.wait(lock, [inst]() {
        return !inst->running ||
               (inst->renderer && inst->renderer->needsRender());
      });
      if (!inst->running) break;

      auto now = Clock::now();
      float dt = std::min(std::chrono::duration<float>(now - prev).count(), 0.05f);
      prev = now;

      if (inst->renderer && inst->pixelBuffer) {
        frameRendered = inst->renderer->renderToBuffer(dt, inst->pixelBuffer.get());
        stillNeedsRender = inst->renderer->needsRender();
      }
    }

    if (frameRendered && inst->textureRegistrar && inst->flTexture) {
      fl_texture_registrar_mark_texture_frame_available(
          inst->textureRegistrar, inst->flTexture);
    }

    if (stillNeedsRender && inst->running) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          Clock::now() - prev).count();
      int sleepMs = kFrameMs - (int)elapsed;
      if (sleepMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
      }
    }
  }
}

static void HandleViewMethodCall(
    FlMethodChannel* /*channel*/,
    FlMethodCall* method_call,
    gpointer user_data) {
  auto* inst = static_cast<LinuxCubeViewInstance*>(user_data);
  if (!inst) return;

  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);
  g_autoptr(FlMethodResponse) response = nullptr;
  bool shouldWake = false;

  {
    std::lock_guard<std::mutex> lock(inst->rendererMutex);
    auto* r = inst->renderer.get();

    if (strcmp(method, "applyAlgorithm") == 0) {
      std::string alg = GetFlString(args, "algorithm");
      if (r && !alg.empty()) { r->applyAlgorithm(alg); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "reset") == 0) {
      if (r) { r->reset(); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "play") == 0) {
      if (r) { r->play(); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "pause") == 0) {
      if (r) { r->pause(); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "stepForward") == 0) {
      if (r) { r->stepForward(); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "stepBackward") == 0) {
      if (r) { r->stepBackward(); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "seekFraction") == 0) {
      double f = GetFlDouble(args, "fraction", 0.0);
      if (r) { r->seekFraction((float)f); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "getCurrentFraction") == 0) {
      double frac = r ? r->currentFraction() : 0.0;
      g_autoptr(FlValue) val = fl_value_new_float(frac);
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(val));

    } else if (strcmp(method, "setSpeed") == 0) {
      double s = GetFlDouble(args, "speed", 1.0);
      if (r) { r->setSpeed((float)s); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "getSpeed") == 0) {
      double s = r ? r->getSpeed() : 1.0;
      g_autoptr(FlValue) val = fl_value_new_float(s);
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(val));

    } else if (strcmp(method, "isAnimating") == 0) {
      bool v = r && r->isAnimating();
      g_autoptr(FlValue) val = fl_value_new_bool(v);
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(val));
    } else if (strcmp(method, "isPlaying") == 0) {
      bool v = r && r->isPlaying();
      g_autoptr(FlValue) val = fl_value_new_bool(v);
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(val));

    } else if (strcmp(method, "setBackgroundColor") == 0) {
      if (r && args) {
        float rr = (float)GetFlDouble(args, "r", 0.08);
        float gg = (float)GetFlDouble(args, "g", 0.08);
        float bb = (float)GetFlDouble(args, "b", 0.10);
        float aa = (float)GetFlDouble(args, "a", 1.0);
        r->setBackgroundColor(rr, gg, bb, aa);
        shouldWake = true;
      }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "setCameraPosition") == 0) {
      if (r && args) {
        float lat = (float)GetFlDouble(args, "latitude", 35.0);
        float lon = (float)GetFlDouble(args, "longitude", 30.0);
        float rad = (float)GetFlDouble(args, "radius", 6.0);
        r->setCameraPosition(lat, lon, rad);
        shouldWake = true;
      }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "setTouchEnabled") == 0) {
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "setPitchLock") == 0) {
      bool locked = GetFlBool(args, "locked", true);
      if (r) { r->setPitchLock(locked); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "setShowHint") == 0) {
      bool enabled = GetFlBool(args, "enabled", false);
      if (r) { r->setShowHint(enabled); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "setBodyAlpha") == 0) {
      double alpha = GetFlDouble(args, "alpha", 1.0);
      if (r) { r->setBodyAlpha((float)alpha); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "setFaceColors") == 0) {
      if (r && args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
        FlValue* list = fl_value_lookup_string(args, "colors");
        if (list && fl_value_get_type(list) == FL_VALUE_TYPE_LIST &&
            fl_value_get_length(list) >= 18) {
          float fc[6][3] = {};
          for (int i = 0; i < 6; ++i) {
            for (int j = 0; j < 3; ++j) {
              FlValue* elem = fl_value_get_list_value(list, i * 3 + j);
              if (elem && fl_value_get_type(elem) == FL_VALUE_TYPE_FLOAT) {
                fc[i][j] = (float)fl_value_get_float(elem);
              }
            }
          }
          r->setFaceColors(fc);
          shouldWake = true;
        }
      }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

    } else if (strcmp(method, "onDragBegin") == 0) {
      if (r && args) {
        r->onDragBegin((float)GetFlDouble(args, "x"), (float)GetFlDouble(args, "y"));
        shouldWake = true;
      }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "onDragMove") == 0) {
      if (r && args) {
        r->onDragMove((float)GetFlDouble(args, "x"), (float)GetFlDouble(args, "y"));
        shouldWake = true;
      }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "onDragEnd") == 0) {
      if (r) { r->onDragEnd(); shouldWake = true; }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (strcmp(method, "onZoom") == 0) {
      if (r && args) {
        r->onZoom((float)GetFlDouble(args, "delta"));
        shouldWake = true;
      }
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else {
      response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    }
  }

  if (shouldWake) {
    inst->wakeCv.notify_one();
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void DisposeInstance(LinuxCubeViewInstance* inst) {
  if (!inst) return;
  inst->running = false;
  inst->wakeCv.notify_all();
  if (inst->renderThread.joinable()) inst->renderThread.join();

  if (inst->viewChannel) {
    fl_method_channel_set_method_call_handler(inst->viewChannel, nullptr, nullptr, nullptr);
    g_object_unref(inst->viewChannel);
    inst->viewChannel = nullptr;
  }
  if (inst->textureRegistrar && inst->flTexture) {
    fl_texture_registrar_unregister_texture(inst->textureRegistrar, inst->flTexture);
    g_object_unref(inst->flTexture);
    inst->flTexture = nullptr;
  }
  if (inst->renderer) {
    inst->renderer->destroy();
  }
}

static void HandleMainMethodCall(
    TwizzledartPlugin* self,
    FlMethodCall* method_call) {
  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);
  g_autoptr(FlMethodResponse) response = nullptr;

  if (strcmp(method, "createView") == 0) {
    uint32_t width  = (uint32_t)GetFlDouble(args, "width",  512);
    uint32_t height = (uint32_t)GetFlDouble(args, "height", 512);

    std::string initialAlgorithm = GetFlString(args, "initialAlgorithm");
    double speed                 = GetFlDouble(args, "speed",       1.0);
    bool   pitchLock             = GetFlBool  (args, "pitchLock",   true);
    double bodyAlpha             = GetFlDouble(args, "bodyAlpha",   1.0);

    double bgR = 0.08, bgG = 0.08, bgB = 0.10;
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* bgMap = fl_value_lookup_string(args, "backgroundColor");
      if (bgMap && fl_value_get_type(bgMap) == FL_VALUE_TYPE_MAP) {
        bgR = GetFlDouble(bgMap, "r", 0.08);
        bgG = GetFlDouble(bgMap, "g", 0.08);
        bgB = GetFlDouble(bgMap, "b", 0.10);
      }
    }

    float faceColors[6][3] = {};
    bool hasFaceColors = false;
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* fcList = fl_value_lookup_string(args, "faceColors");
      if (fcList && fl_value_get_type(fcList) == FL_VALUE_TYPE_LIST &&
          fl_value_get_length(fcList) >= 18) {
        hasFaceColors = true;
        for (int i = 0; i < 6; ++i) {
          for (int j = 0; j < 3; ++j) {
            FlValue* elem = fl_value_get_list_value(fcList, i * 3 + j);
            if (elem && fl_value_get_type(elem) == FL_VALUE_TYPE_FLOAT) {
              faceColors[i][j] = (float)fl_value_get_float(elem);
            }
          }
        }
      }
    }

    double camLat = 35.0, camLon = 30.0, camRad = 6.0;
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* camMap = fl_value_lookup_string(args, "cameraPosition");
      if (camMap && fl_value_get_type(camMap) == FL_VALUE_TYPE_MAP) {
        camLat = GetFlDouble(camMap, "latitude",  35.0);
        camLon = GetFlDouble(camMap, "longitude", 30.0);
        camRad = GetFlDouble(camMap, "radius",     6.0);
      }
    }

    auto inst = std::make_unique<LinuxCubeViewInstance>();
    inst->bufferWidth  = width;
    inst->bufferHeight = height;
    inst->pixelBuffer  = std::make_unique<uint8_t[]>(width * height * 4);
    inst->textureRegistrar =
        fl_plugin_registrar_get_texture_registrar(self->registrar);

    TwizzleLinuxTexture* tex = twizzle_linux_texture_new(
        &inst->rendererMutex, inst->pixelBuffer.get(), width, height);
    inst->flTexture = FL_TEXTURE(tex);
    fl_texture_registrar_register_texture(inst->textureRegistrar, inst->flTexture);
    inst->textureId = fl_texture_get_id(inst->flTexture);

    auto renderer = std::make_unique<twizzle::renderer::WebGpuRenderer>();
    twizzle::renderer::SurfaceDescriptor surfDesc;
    surfDesc.width       = width;
    surfDesc.height      = height;
    surfDesc.isOffscreen = true;

    if (renderer->init(surfDesc)) {
      renderer->setBackgroundColor((float)bgR, (float)bgG, (float)bgB, 1.0f);
      renderer->setSpeed((float)speed);
      renderer->setPitchLock(pitchLock);
      renderer->setCameraPosition((float)camLat, (float)camLon, (float)camRad);
      renderer->setBodyAlpha((float)bodyAlpha);
      if (hasFaceColors) renderer->setFaceColors(faceColors);
      if (!initialAlgorithm.empty()) renderer->applyAlgorithm(initialAlgorithm);
    }
    inst->renderer = std::move(renderer);

    std::string viewChannelName = "twizzledart_view_" + std::to_string(inst->textureId);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
    inst->viewChannel = fl_method_channel_new(
        fl_plugin_registrar_get_messenger(self->registrar),
        viewChannelName.c_str(),
        FL_METHOD_CODEC(codec));

    auto* rawInst = inst.get();
    fl_method_channel_set_method_call_handler(
        inst->viewChannel, HandleViewMethodCall, rawInst, nullptr);

    inst->running = true;
    inst->renderThread = std::thread([rawInst]() { RenderLoop(rawInst); });

    int64_t registeredId = inst->textureId;
    {
      std::lock_guard<std::mutex> lock(self->instancesMutex);
      self->instances[registeredId] = std::move(inst);
    }

    g_autoptr(FlValue) resVal = fl_value_new_int(registeredId);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(resVal));

  } else if (strcmp(method, "disposeView") == 0) {
    int64_t texId = (int64_t)GetFlDouble(args, "textureId", -1);
    std::unique_ptr<LinuxCubeViewInstance> inst;
    {
      std::lock_guard<std::mutex> lock(self->instancesMutex);
      auto it = self->instances.find(texId);
      if (it != self->instances.end()) {
        inst = std::move(it->second);
        self->instances.erase(it);
      }
    }
    if (inst) DisposeInstance(inst.get());
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void method_call_cb(
    FlMethodChannel* /*channel*/,
    FlMethodCall* method_call,
    gpointer user_data) {
  TwizzledartPlugin* plugin = static_cast<TwizzledartPlugin*>(user_data);
  HandleMainMethodCall(plugin, method_call);
}

static void twizzledart_plugin_dispose(GObject* object) {
  TwizzledartPlugin* self = reinterpret_cast<TwizzledartPlugin*>(object);
  {
    std::lock_guard<std::mutex> lock(self->instancesMutex);
    for (auto& [id, inst] : self->instances) {
      DisposeInstance(inst.get());
    }
    self->instances.clear();
  }
  if (self->mainChannel) {
    g_object_unref(self->mainChannel);
    self->mainChannel = nullptr;
  }
  if (self->registrar) {
    g_object_unref(self->registrar);
    self->registrar = nullptr;
  }
  self->instances.~unordered_map();
  self->instancesMutex.~mutex();
  G_OBJECT_CLASS(twizzledart_plugin_parent_class)->dispose(object);
}

static void twizzledart_plugin_class_init(TwizzledartPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = twizzledart_plugin_dispose;
}

static void twizzledart_plugin_init(TwizzledartPlugin* self) {
  new (&self->instancesMutex) std::mutex();
  new (&self->instances)
      std::unordered_map<int64_t, std::unique_ptr<LinuxCubeViewInstance>>();
}

void twizzledart_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  TwizzledartPlugin* plugin = reinterpret_cast<TwizzledartPlugin*>(
      g_object_new(twizzledart_plugin_get_type(), nullptr));
  plugin->registrar = FL_PLUGIN_REGISTRAR(g_object_ref(registrar));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  plugin->mainChannel = fl_method_channel_new(
      fl_plugin_registrar_get_messenger(registrar),
      "twizzledart",
      FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(
      plugin->mainChannel, method_call_cb, g_object_ref(plugin), g_object_unref);

  g_object_unref(plugin);
}
