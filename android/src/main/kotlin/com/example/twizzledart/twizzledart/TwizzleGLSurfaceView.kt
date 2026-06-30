package com.example.twizzledart.twizzledart

import android.content.Context
import android.opengl.EGL14
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

// ---------------------------------------------------------------------------
// TwizzleGLSurfaceView
// ---------------------------------------------------------------------------
/**
 * Drop-in GLSurfaceView that renders the Rubik's cube via native C++ renderer.
 *
 * Usage in your Activity/Fragment:
 *
 *   val cubeView = TwizzleGLSurfaceView(this)
 *   setContentView(cubeView)
 *
 *   // Apply moves from anywhere:
 *   cubeView.applyAlgorithm("R U R' U'")
 *   cubeView.reset()
 */
class TwizzleGLSurfaceView(context: Context) : GLSurfaceView(context) {

    private val renderer = TwizzleRenderer()

    // Touch state
    var touchEnabled = true
    var onTapListener: (() -> Unit)? = null
    private var isClickGesture = false
    private var lastTouchX = 0f
    private var lastTouchY = 0f
    private var pointerCount = 0
    private var lastSpan = 0f

    private val scaleDetector = ScaleGestureDetector(context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScaleBegin(d: ScaleGestureDetector): Boolean {
                lastSpan = d.currentSpan; return true
            }
            override fun onScale(d: ScaleGestureDetector): Boolean {
                val delta = (d.currentSpan - lastSpan) * 0.01f
                queueEvent { renderer.nativeOnZoom(-delta) }
                lastSpan = d.currentSpan
                return true
            }
        })

    init {
        setEGLContextClientVersion(3) // OpenGL ES 3.0

        // Request MSAA (4×) — optional but improves quality
        setEGLConfigChooser(8, 8, 8, 8, 16, 4)

        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    // ── Input ──────────────────────────────────────────────────────────────

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (!touchEnabled) {
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    isClickGesture = true
                    lastTouchX = e.x
                    lastTouchY = e.y
                }
                MotionEvent.ACTION_UP -> {
                    if (isClickGesture) {
                        onTapListener?.invoke()
                    }
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = Math.abs(e.x - lastTouchX)
                    val dy = Math.abs(e.y - lastTouchY)
                    if (dx > 10 || dy > 10) {
                        isClickGesture = false
                    }
                }
            }
            return true
        }

        scaleDetector.onTouchEvent(e)

        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                pointerCount = e.pointerCount
                lastTouchX   = e.x
                lastTouchY   = e.y
            }
            MotionEvent.ACTION_MOVE -> {
                if (!scaleDetector.isInProgress && e.pointerCount == 1) {
                    val dx = e.x - lastTouchX
                    val dy = e.y - lastTouchY
                    queueEvent { renderer.nativeOnDrag(dx, dy) }
                }
                lastTouchX = e.x
                lastTouchY = e.y
            }
        }
        return true
    }

    // ── Public API ─────────────────────────────────────────────────────────

    fun applyAlgorithm(alg: String) {
        queueEvent { renderer.nativeApplyAlgorithm(alg) }
    }

    fun reset() {
        queueEvent { renderer.nativeReset() }
    }

    fun isAnimating(): Boolean = renderer.nativeIsAnimating()

    fun play() {
        queueEvent { renderer.nativePlay() }
    }

    fun pause() {
        queueEvent { renderer.nativePause() }
    }

    fun stepForward() {
        queueEvent { renderer.nativeStepForward() }
    }

    fun stepBackward() {
        queueEvent { renderer.nativeStepBackward() }
    }

    fun isPlaying(): Boolean = renderer.nativeIsPlaying()

    /** Animation speed: 1.0 = default, 2.0 = twice as fast, 0.5 = half speed. */
    fun setSpeed(speed: Float) {
        queueEvent { renderer.nativeSetSpeed(speed) }
    }

    fun getSpeed(): Float = renderer.nativeGetSpeed()

    fun setGLBackgroundColor(r: Float, g: Float, b: Float, a: Float) {
        renderer.cachedBgColor = floatArrayOf(r, g, b, a)
        queueEvent { renderer.nativeSetBackgroundColor(r, g, b, a) }
    }

    fun setCameraPosition(lat: Float, lon: Float, rad: Float) {
        renderer.cachedCameraPosition = floatArrayOf(lat, lon, rad)
        queueEvent { renderer.nativeSetCameraPosition(lat, lon, rad) }
    }

    fun setShowHint(enabled: Boolean) {
        renderer.cachedShowHint = enabled
        queueEvent { renderer.nativeSetShowHint(enabled) }
    }

    /** Sets face colours as 18 floats (6 faces × RGB). */
    fun setFaceColors(colors: FloatArray) {
        renderer.cachedFaceColors = colors
        queueEvent { renderer.nativeSetFaceColors(colors) }
    }

    /** Sets cubie body (foundation) alpha: 0.3 = crystal, 1.0 = opaque black. */
    fun setBodyAlpha(alpha: Float) {
        renderer.cachedBodyAlpha = alpha
        queueEvent { renderer.nativeSetBodyAlpha(alpha) }
    }

    fun seekFraction(f: Float) {
        queueEvent { renderer.nativeSeekFraction(f) }
    }

    fun getCurrentFraction(): Float {
        return renderer.nativeCurrentFraction()
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    override fun onPause() {
        super.onPause()
        queueEvent { renderer.nativeOnDestroy() }
    }
}

// ---------------------------------------------------------------------------
// TwizzleRenderer — GLSurfaceView.Renderer delegating to C++ via JNI
// ---------------------------------------------------------------------------
class TwizzleRenderer : GLSurfaceView.Renderer {

    private var lastNano = System.nanoTime()

    var cachedBgColor: FloatArray? = null
    var cachedCameraPosition: FloatArray? = null
    var cachedShowHint: Boolean? = null
    var cachedFaceColors: FloatArray? = null
    var cachedBodyAlpha: Float? = null

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        nativeOnSurfaceCreated()
        lastNano = System.nanoTime()

        cachedBgColor?.let {
            nativeSetBackgroundColor(it[0], it[1], it[2], it[3])
        }
        cachedCameraPosition?.let {
            nativeSetCameraPosition(it[0], it[1], it[2])
        }
        cachedShowHint?.let {
            nativeSetShowHint(it)
        }
        cachedFaceColors?.let {
            nativeSetFaceColors(it)
        }
        cachedBodyAlpha?.let {
            nativeSetBodyAlpha(it)
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        nativeOnSurfaceChanged(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        val now  = System.nanoTime()
        val dt   = (now - lastNano) / 1_000_000_000f
        lastNano = now
        nativeOnDrawFrame(dt.coerceIn(0f, 0.05f))
    }

    // ── JNI declarations ──────────────────────────────────────────────────

    external fun nativeOnSurfaceCreated()
    external fun nativeOnSurfaceChanged(width: Int, height: Int)
    external fun nativeOnDrawFrame(dt: Float)
    external fun nativeOnDestroy()

    external fun nativeOnDrag(dx: Float, dy: Float)
    external fun nativeOnZoom(delta: Float)

    external fun nativeApplyAlgorithm(alg: String)
    external fun nativeReset()
    external fun nativeIsAnimating(): Boolean
    external fun nativeSetSpeed(speed: Float)
    external fun nativeGetSpeed(): Float
    external fun nativePlay()
    external fun nativePause()
    external fun nativeStepForward()
    external fun nativeStepBackward()
    external fun nativeIsPlaying(): Boolean
    external fun nativeSetBackgroundColor(r: Float, g: Float, b: Float, a: Float)
    external fun nativeSetCameraPosition(lat: Float, lon: Float, rad: Float)
    external fun nativeSeekFraction(f: Float)
    external fun nativeCurrentFraction(): Float
    external fun nativeSetShowHint(enabled: Boolean)
    external fun nativeSetFaceColors(colors: FloatArray)
    external fun nativeSetBodyAlpha(alpha: Float)

    companion object {
        init { System.loadLibrary("twizzle") }
    }
}
