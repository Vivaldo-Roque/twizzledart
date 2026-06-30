#pragma once
#include "gl_platform.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "CubeGeometry.hpp"
#include "MoveAnimator.hpp"
#include "twizzle/twizzle.hpp"
#include <memory>
#include <string>

namespace twizzle::renderer {

/**
 * @brief Top-level OpenGL renderer for a 3x3x3 Rubik's Cube.
 *
 * Owns the GL resources (VAO/VBO/shader), the orbit camera, and the move
 * timeline (MoveAnimator). Platform glue code (desktop/Android/iOS) only
 * needs to call init()/resize()/render() and forward input + move/transport
 * requests — all rendering and animation logic lives here.
 */
class CubeRenderer {
public:
    CubeRenderer();
    ~CubeRenderer();

    /// Compiles shaders and uploads geometry. Call once after GL context creation.
    bool init();
    /// Updates the viewport and camera aspect ratio. Call on every resize.
    void resize(int width, int height);
    /// Advances animation and draws one frame. @param dt seconds since last frame.
    void render(float dt);
    /// Releases all GL resources. Call before destroying the GL context.
    void destroy();

    // ── Camera input ────────────────────────────────────────────────────
    /// Begin a drag gesture (enables post-release inertia).
    void onDragBegin(float x, float y);
    /// Continue a drag gesture with the current pointer position.
    void onDragMove(float x, float y);
    /// End a drag gesture.
    void onDragEnd();
    /// Simple delta-based drag, for platforms without begin/move/end events.
    void onDrag(float dx, float dy);
    /// Zoom in (`delta` > 0) or out.
    void onZoom(float delta);

    // ── Building the move sequence ──────────────────────────────────────
    /// Appends a move and jumps the playhead to the end (do it now).
    void applyMove(const twizzle::Move& move);
    /// Parses and appends every move in a WCA notation algorithm string.
    void applyAlgorithm(const std::string& algStr);
    /// Clears the timeline and returns to the solved state.
    void reset();

    // ── Transport controls — mirrors cubing.js TwistyButtons ────────────
    void play();
    void pause();
    void togglePlay();
    void jumpToStart();
    void jumpToEnd();
    void stepForward();
    void stepBackward();

    // ── Scrubbing — mirrors cubing.js TwistyScrubber ─────────────────────
    /// Seeks to a fraction of the full timeline, in [0,1].
    void  seekFraction(float f);
    /// Seeks to an absolute timestamp, in milliseconds.
    void  seekMs(float ms);
    float currentFraction() const;
    float currentMs()       const;
    float totalDurationMs() const;

    bool isPlaying()   const;
    bool isAnimating() const;

    /// Playback speed multiplier, clamped to [0.1, 8.0]. 1.0 = default.
    void  setSpeed(float speed);
    float getSpeed() const;

    /// Configures the viewport clear color.
    void setBackgroundColor(float r, float g, float b, float a);

    /// Sets the OrbitCamera position.
    void setCameraPosition(float latitude, float longitude, float radius);

private:
    GLuint vao_ = 0, vbo_ = 0;
    Shader shader_;

    std::vector<Vertex>    vertices_;
    std::vector<CubieInfo> cubies_;

    OrbitCamera                   camera_;
    std::unique_ptr<MoveAnimator> animator_;
    twizzle::Cube3x3              cube_;

    bool  initialized_ = false;
    float startTime_   = 0.0f;
    int   viewWidth_   = 1, viewHeight_ = 1;

    float bgR_ = 0.08f, bgG_ = 0.08f, bgB_ = 0.10f, bgA_ = 1.0f;

    bool  dragging_  = false;
    float lastDragX_ = 0, lastDragY_ = 0;

    /// Uploads `vertices_` into vbo_ and configures vertex attributes.
    void  uploadGeometry();
    /// @return a monotonic wall-clock timestamp in seconds.
    static float nowSeconds();
};

} // namespace twizzle::renderer
