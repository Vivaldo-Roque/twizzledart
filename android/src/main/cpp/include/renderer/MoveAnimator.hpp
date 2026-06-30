#pragma once
#include "gl_platform.hpp"
#include "CubeGeometry.hpp"
#include "twizzle/cube.hpp"
#include <vector>
#include <algorithm>

namespace twizzle::renderer {

/// Rotation parameters (axis, angle, affected slice) derived from a Move.
struct MoveParams {
    glm::vec3 axis;       ///< world-space rotation axis
    float     totalAngle; ///< signed final angle in radians
    int       sliceAxis;  ///< 0=X, 1=Y, 2=Z
    int       sliceSign;  ///< -1/0/+1 = which slice; 9 = whole-cube rotation
};

/// @return the axis/angle/slice for a single Move (right-hand rule).
MoveParams paramsForMove(const twizzle::Move& move);

/**
 * @brief Scrubbable timeline of cube moves, mirroring cubing.js's player model.
 *
 * Every move occupies a fixed-duration slot on the timeline. Internally,
 * a "checkpoint" (the model matrix of every cubie) is cached after each
 * move, so seeking to any timestamp — forward or backward — is O(1):
 * look up the checkpoint before the move under the playhead, then apply
 * the in-progress rotation for that partial move.
 */
class MoveAnimator {
public:
    /// Per-move duration on the timeline, in milliseconds.
    static constexpr float kDefaultMoveDurationMs = 200.0f;

    explicit MoveAnimator(std::vector<CubieInfo>& cubies);

    /// Appends a move to the end of the timeline. Does not move the playhead.
    void appendMove(const twizzle::Move& move);
    /// Clears the timeline back to an empty, solved state.
    void clear();

    // ── Transport controls (mirrors cubing.js TwistyButtons) ──────────────
    void play();
    void pause();
    void togglePlay();
    void jumpToStart();
    void jumpToEnd();
    /// Advances the playhead to the next move boundary.
    void stepForward();
    /// Retreats the playhead to the previous move boundary.
    void stepBackward();

    // ── Scrubbing (mirrors cubing.js TwistyScrubber) ───────────────────────
    /// Sets the playhead to an absolute time, clamped to the valid range.
    void  seekMs(float ms);
    /// Sets the playhead as a fraction of the total duration, clamped [0,1].
    void  seekFraction(float f);
    float currentMs()       const { return playheadMs_; }
    float totalDurationMs() const { return (float)moves_.size() * moveDurationMs_; }
    float currentFraction() const;

    /// Advances the playhead while playing(). Call once per frame.
    void update(float dt);

    /// @return the world model matrix of cubie[cubieIndex] at the playhead.
    glm::mat4 modelMatrix(int cubieIndex) const;
    bool      isPlaying()   const { return playing_; }
    bool      isAnimating() const { return playing_; }
    int       moveCount()   const { return (int)moves_.size(); }
    /// @return index of the move under the playhead, in [0, moveCount()].
    int       currentMoveIndex() const;

    /// Playback speed multiplier (does not change move duration on the
    /// timeline — only how fast the playhead advances while playing()).
    void  setSpeed(float s) { speed_ = std::clamp(s, 0.1f, 8.0f); }
    float getSpeed() const  { return speed_; }

    /// Convenience: append a move and jump straight to the end of the
    /// timeline (the common case for live keyboard/gesture input).
    void queueMove(const twizzle::Move& move) { appendMove(move); jumpToEnd(); }

private:
    std::vector<CubieInfo>& cubies_;
    std::vector<twizzle::Move> moves_;

    /// checkpoints_[i] = model matrices of all cubies after i moves.
    std::vector<std::vector<glm::mat4>> checkpoints_;

    float playheadMs_     = 0.0f;
    float moveDurationMs_ = kDefaultMoveDurationMs;
    bool  playing_        = false;
    float speed_          = 1.0f;

    /// Rebuilds checkpoints_ from scratch by replaying every move.
    void recomputeCheckpoints();
};

} // namespace twizzle::renderer
