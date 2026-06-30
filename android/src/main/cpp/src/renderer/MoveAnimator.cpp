/// @file MoveAnimator.cpp
/// @brief Implements the scrubbable move timeline: checkpoint replay +
/// smootherStep-eased partial rotations.
#include "renderer/MoveAnimator.hpp"
#include "renderer/easing.hpp"
#include <cmath>

namespace twizzle::renderer {
using namespace twizzle;

// Move → rotation params (axis + angle), right-hand rule per face.
MoveParams paramsForMove(const Move& move) {
    MoveParams p; p.sliceSign = 0; p.sliceAxis = 0;
    float base = glm::radians(-90.0f);

    switch (move.face) {
    case Face::R: p.axis={1,0,0}; base=-glm::radians(90.f); p.sliceAxis=0; p.sliceSign=+1; break;
    case Face::L: p.axis={1,0,0}; base=+glm::radians(90.f); p.sliceAxis=0; p.sliceSign=-1; break;
    case Face::U: p.axis={0,1,0}; base=-glm::radians(90.f); p.sliceAxis=1; p.sliceSign=+1; break;
    case Face::D: p.axis={0,1,0}; base=+glm::radians(90.f); p.sliceAxis=1; p.sliceSign=-1; break;
    case Face::F: p.axis={0,0,1}; base=-glm::radians(90.f); p.sliceAxis=2; p.sliceSign=+1; break;
    case Face::B: p.axis={0,0,1}; base=+glm::radians(90.f); p.sliceAxis=2; p.sliceSign=-1; break;
    case Face::M: p.axis={1,0,0}; base=+glm::radians(90.f); p.sliceAxis=0; p.sliceSign= 0; break;
    case Face::E: p.axis={0,1,0}; base=+glm::radians(90.f); p.sliceAxis=1; p.sliceSign= 0; break;
    case Face::S: p.axis={0,0,1}; base=-glm::radians(90.f); p.sliceAxis=2; p.sliceSign= 0; break;
    case Face::X: p.axis={1,0,0}; base=-glm::radians(90.f); p.sliceAxis=0; p.sliceSign= 9; break;
    case Face::Y: p.axis={0,1,0}; base=-glm::radians(90.f); p.sliceAxis=1; p.sliceSign= 9; break;
    case Face::Z: p.axis={0,0,1}; base=-glm::radians(90.f); p.sliceAxis=2; p.sliceSign= 9; break;
    default: p.axis={1,0,0}; break;
    }

    float mult = 1.0f;
    if (move.dir == Direction::CCW)    mult = -1.0f;
    if (move.dir == Direction::DOUBLE) mult =  2.0f;
    p.totalAngle = base * mult;
    return p;
}

// ---------------------------------------------------------------------------
MoveAnimator::MoveAnimator(std::vector<CubieInfo>& cubies) : cubies_(cubies) {
    recomputeCheckpoints();
}

// ---------------------------------------------------------------------------
// recomputeCheckpoints — replays the full move list from solved state,
// recording the cubie model matrices after every move. This is what makes
// O(1) scrubbing to an arbitrary timestamp possible.
// ---------------------------------------------------------------------------
void MoveAnimator::recomputeCheckpoints() {
    const int n = (int)cubies_.size();
    checkpoints_.clear();
    checkpoints_.reserve(moves_.size() + 1);

    // Live simulation state: current matrix + current integer grid position
    // for each cubie (grid position is needed to know which slice each
    // subsequent move affects).
    std::vector<glm::mat4>  curMat(n);
    std::vector<glm::ivec3> curGrid(n);
    for (int i = 0; i < n; ++i) {
        curGrid[i] = cubies_[i].gridPos;
        curMat[i]  = glm::translate(glm::mat4(1.0f), glm::vec3(curGrid[i]));
    }
    checkpoints_.push_back(curMat); // checkpoint 0 = solved

    for (const Move& mv : moves_) {
        MoveParams p = paramsForMove(mv);
        glm::mat4 rot = glm::rotate(glm::mat4(1.0f), p.totalAngle, p.axis);

        for (int i = 0; i < n; ++i) {
            int coord = (p.sliceAxis==0) ? curGrid[i].x
                       : (p.sliceAxis==1) ? curGrid[i].y
                                          : curGrid[i].z;
            if (p.sliceSign == 9 || coord == p.sliceSign) {
                curMat[i]  = rot * curMat[i];
                glm::vec4 np = rot * glm::vec4(glm::vec3(curGrid[i]), 1.0f);
                curGrid[i] = glm::ivec3(glm::round(glm::vec3(np)));
            }
        }
        checkpoints_.push_back(curMat);
    }
}

// ---------------------------------------------------------------------------
void MoveAnimator::appendMove(const Move& move) {
    moves_.push_back(move);
    // Extend checkpoints incrementally (cheap — no need to replay from 0).
    const auto& prev = checkpoints_.back();
    std::vector<glm::mat4> next = prev;

    MoveParams p = paramsForMove(move);
    glm::mat4  rot = glm::rotate(glm::mat4(1.0f), p.totalAngle, p.axis);

    // Reconstruct live grid positions implied by `prev` to know which
    // cubies this move affects (decompose translation from accumulated
    // rotation+translation matrix — exact since cubie transforms are
    // pure rotation about origin composed with an initial unit translation,
    // so the translation column already IS the current grid position).
    for (int i = 0; i < (int)cubies_.size(); ++i) {
        glm::vec3 pos = glm::vec3(prev[i][3]); // translation column
        glm::ivec3 grid = glm::ivec3(glm::round(pos));
        int coord = (p.sliceAxis==0) ? grid.x : (p.sliceAxis==1) ? grid.y : grid.z;
        if (p.sliceSign == 9 || coord == p.sliceSign) {
            next[i] = rot * prev[i];
        }
    }
    checkpoints_.push_back(next);
}

void MoveAnimator::clear() {
    moves_.clear();
    playheadMs_ = 0.0f;
    playing_    = false;
    recomputeCheckpoints();
}

// ---------------------------------------------------------------------------
void MoveAnimator::play()       { if (!moves_.empty()) playing_ = true; }
void MoveAnimator::pause()      { playing_ = false; }
void MoveAnimator::togglePlay() {
    if (playing_) { pause(); return; }
    // Like cubing.js: if at the very end, restart from the beginning.
    if (playheadMs_ >= totalDurationMs() - 0.001f) playheadMs_ = 0.0f;
    play();
}

void MoveAnimator::jumpToStart() { playheadMs_ = 0.0f; playing_ = false; }
void MoveAnimator::jumpToEnd()   { playheadMs_ = totalDurationMs(); playing_ = false; }

void MoveAnimator::stepForward() {
    playing_ = false;
    int idx = currentMoveIndex();
    float target = (float)std::min(idx + 1, (int)moves_.size()) * moveDurationMs_;
    playheadMs_ = target;
}

void MoveAnimator::stepBackward() {
    playing_ = false;
    // If mid-move, snap to the start of the current move; otherwise go
    // back one full move (matches "play-step-backwards" boundary semantics).
    int idx = currentMoveIndex();
    float curBoundary = (float)idx * moveDurationMs_;
    if (playheadMs_ - curBoundary > 0.5f) {
        playheadMs_ = curBoundary;
    } else {
        playheadMs_ = (float)std::max(idx - 1, 0) * moveDurationMs_;
    }
}

// ---------------------------------------------------------------------------
void MoveAnimator::seekMs(float ms) {
    playheadMs_ = std::clamp(ms, 0.0f, totalDurationMs());
}
void MoveAnimator::seekFraction(float f) {
    seekMs(std::clamp(f, 0.0f, 1.0f) * totalDurationMs());
}
float MoveAnimator::currentFraction() const {
    float total = totalDurationMs();
    return total > 0.0f ? playheadMs_ / total : 0.0f;
}

int MoveAnimator::currentMoveIndex() const {
    if (moveDurationMs_ <= 0.0f) return 0;
    int idx = (int)std::floor(playheadMs_ / moveDurationMs_);
    return std::clamp(idx, 0, (int)moves_.size());
}

// ---------------------------------------------------------------------------
void MoveAnimator::update(float dt) {
    if (!playing_) return;
    playheadMs_ += dt * 1000.0f * speed_;
    if (playheadMs_ >= totalDurationMs()) {
        playheadMs_ = totalDurationMs();
        playing_    = false;
    }
}

// ---------------------------------------------------------------------------
glm::mat4 MoveAnimator::modelMatrix(int index) const {
    int n = (int)moves_.size();
    int moveIdx = currentMoveIndex();

    if (moveIdx >= n) return checkpoints_[n][index];

    float boundaryMs = (float)moveIdx * moveDurationMs_;
    float t = (playheadMs_ - boundaryMs) / moveDurationMs_;
    t = std::clamp(t, 0.0f, 1.0f);

    MoveParams p = paramsForMove(moves_[moveIdx]);
    const glm::mat4& base = checkpoints_[moveIdx][index];

    glm::vec3 pos  = glm::vec3(base[3]);
    glm::ivec3 grid = glm::ivec3(glm::round(pos));
    int coord = (p.sliceAxis==0) ? grid.x : (p.sliceAxis==1) ? grid.y : grid.z;
    bool affected = (p.sliceSign == 9 || coord == p.sliceSign);
    if (!affected) return base;

    float eased = smootherStep(t);
    float angle = p.totalAngle * eased;
    return glm::rotate(glm::mat4(1.0f), angle, p.axis) * base;
}

} // namespace twizzle::renderer
