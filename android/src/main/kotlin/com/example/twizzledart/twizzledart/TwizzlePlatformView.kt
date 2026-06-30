package com.example.twizzledart.twizzledart

import android.content.Context
import android.view.View
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.platform.PlatformView

class TwizzlePlatformView(
    context: Context,
    id: Int,
    creationParams: Map<String, Any>?,
    messenger: BinaryMessenger
) : PlatformView, MethodChannel.MethodCallHandler {

    private val cubeView: TwizzleGLSurfaceView = TwizzleGLSurfaceView(context)
    private val channel: MethodChannel = MethodChannel(messenger, "twizzledart_view_$id")

    init {
        channel.setMethodCallHandler(this)
        
        cubeView.onTapListener = {
            channel.invokeMethod("onTap", null)
        }

        // Initial setup from parameters if any
        creationParams?.get("initialAlgorithm")?.let {
            if (it is String && it.isNotEmpty()) {
                cubeView.applyAlgorithm(it)
            }
        }
        creationParams?.get("speed")?.let {
            if (it is Number) {
                cubeView.setSpeed(it.toFloat())
            }
        }
        creationParams?.get("touchEnabled")?.let {
            if (it is Boolean) {
                cubeView.touchEnabled = it
            }
        }
        creationParams?.get("backgroundColor")?.let {
            if (it is Map<*, *>) {
                val r = (it["r"] as? Number)?.toFloat() ?: 0.08f
                val g = (it["g"] as? Number)?.toFloat() ?: 0.08f
                val b = (it["b"] as? Number)?.toFloat() ?: 0.10f
                val a = (it["a"] as? Number)?.toFloat() ?: 1.0f
                cubeView.setGLBackgroundColor(r, g, b, a)
            }
        }
        creationParams?.get("cameraPosition")?.let {
            if (it is Map<*, *>) {
                val lat = (it["latitude"] as? Number)?.toFloat() ?: 35.0f
                val lon = (it["longitude"] as? Number)?.toFloat() ?: 30.0f
                val rad = (it["radius"] as? Number)?.toFloat() ?: 14.0f
                cubeView.setCameraPosition(lat, lon, rad)
            }
        }
    }

    override fun getView(): View {
        return cubeView
    }

    override fun dispose() {
        cubeView.onPause()
        channel.setMethodCallHandler(null)
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "applyAlgorithm" -> {
                val alg = call.argument<String>("algorithm")
                if (alg != null) {
                    cubeView.applyAlgorithm(alg)
                    result.success(null)
                } else {
                    result.error("INVALID_ARGUMENT", "Algorithm cannot be null", null)
                }
            }
            "reset" -> {
                cubeView.reset()
                result.success(null)
            }
            "setSpeed" -> {
                val speed = call.argument<Double>("speed")
                if (speed != null) {
                    cubeView.setSpeed(speed.toFloat())
                    result.success(null)
                } else {
                    result.error("INVALID_ARGUMENT", "Speed cannot be null", null)
                }
            }
            "getSpeed" -> {
                result.success(cubeView.getSpeed().toDouble())
            }
            "isAnimating" -> {
                result.success(cubeView.isAnimating())
            }
            "play" -> {
                cubeView.play()
                result.success(null)
            }
            "pause" -> {
                cubeView.pause()
                result.success(null)
            }
            "stepForward" -> {
                cubeView.stepForward()
                result.success(null)
            }
            "stepBackward" -> {
                cubeView.stepBackward()
                result.success(null)
            }
            "isPlaying" -> {
                result.success(cubeView.isPlaying())
            }
            "setBackgroundColor" -> {
                val r = call.argument<Double>("r")?.toFloat() ?: 0.08f
                val g = call.argument<Double>("g")?.toFloat() ?: 0.08f
                val b = call.argument<Double>("b")?.toFloat() ?: 0.10f
                val a = call.argument<Double>("a")?.toFloat() ?: 1.0f
                cubeView.setGLBackgroundColor(r, g, b, a)
                result.success(null)
            }
            "setCameraPosition" -> {
                val lat = call.argument<Double>("latitude")?.toFloat() ?: 35.0f
                val lon = call.argument<Double>("longitude")?.toFloat() ?: 30.0f
                val rad = call.argument<Double>("radius")?.toFloat() ?: 14.0f
                cubeView.setCameraPosition(lat, lon, rad)
                result.success(null)
            }
            "seekFraction" -> {
                val f = call.argument<Double>("fraction")?.toFloat() ?: 0.0f
                cubeView.seekFraction(f)
                result.success(null)
            }
            "getCurrentFraction" -> {
                result.success(cubeView.getCurrentFraction().toDouble())
            }
            "setTouchEnabled" -> {
                val enabled = call.argument<Boolean>("enabled") ?: true
                cubeView.touchEnabled = enabled
                result.success(null)
            }
            else -> {
                result.notImplemented()
            }
        }
    }
}
