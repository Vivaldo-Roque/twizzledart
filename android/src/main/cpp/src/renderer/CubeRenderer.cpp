/// @file CubeRenderer.cpp
/// @brief Implements CubeRenderer: flat-shaded GLSL program, 3-pass draw
/// (hint facelets / foundation / stickers) and transport pass-through to
/// MoveAnimator.
#include "renderer/CubeRenderer.hpp"
#include <cstdio>
#include <chrono>

namespace twizzle::renderer {

// Flat color shader — MeshBasicMaterial equivalent (no lighting).
static const char* kVertSrc = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uMVP;

out vec4 vColor;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor      = aColor;
}
)GLSL";

static const char* kFragSrc = R"GLSL(
in  vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)GLSL";

// ---------------------------------------------------------------------------
CubeRenderer::CubeRenderer()  = default;
CubeRenderer::~CubeRenderer() { destroy(); }

bool CubeRenderer::init() {
    if (initialized_) return true;
    if (!shader_.build(kVertSrc, kFragSrc)) {
        std::fprintf(stderr, "[CubeRenderer] Shader build failed\n");
        return false;
    }
    CubeGeometry::build(vertices_, cubies_);
    uploadGeometry();
    animator_ = std::make_unique<MoveAnimator>(cubies_);
    initialized_ = true;
    startTime_   = nowSeconds();
    return true;
}

void CubeRenderer::uploadGeometry() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertices_.size() * sizeof(Vertex)),
                 vertices_.data(), GL_STATIC_DRAW);

    auto stride = (GLsizei)sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, color));
    glBindVertexArray(0);
}

void CubeRenderer::resize(int w, int h) {
    glViewport(0, 0, w, h);
    camera_.setViewport(w, h);
    viewWidth_  = w;
    viewHeight_ = h;
}

// ---------------------------------------------------------------------------
// render — three sub-passes matching cubing.js Cube3D.ts material sides:
//
//   1. Hint facelets  — cull GL_FRONT (== three.js `side: BackSide`)
//                        → visible ONLY when the facelet's outward normal
//                          points AWAY from the camera (i.e. you're seeing
//                          "through" to the position from behind/inside).
//   2. Foundation     — translucent black box, standard back-face culling.
//   3. Stickers       — cull GL_BACK  (== three.js `side: FrontSide`)
//                        → visible ONLY when the outward normal faces
//                          the camera. Always drawn last, opaque, so it
//                          correctly occludes the hint facelet behind it.
// ---------------------------------------------------------------------------
void CubeRenderer::render(float dt) {
    if (!initialized_) return;

    float now = nowSeconds() - startTime_;
    animator_->update(dt);
    camera_.update(dt, now);

    glClearColor(bgR_, bgG_, bgB_, bgA_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);

    shader_.bind();
    glBindVertexArray(vao_);

    glm::mat4 proj = camera_.projection();
    glm::mat4 view = camera_.view();

    // ── Pass 1: Hint facelets (BackSide ≡ cull GL_FRONT) ───────────────────
    // Translucent — don't write depth, so it never blocks the real sticker
    // that gets drawn later in pass 3.
    if (showHint_) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glCullFace(GL_FRONT);

        for (int i = 0; i < (int)cubies_.size(); ++i) {
            if (cubies_[i].hintCount == 0) continue;
            glm::mat4 mvp = proj * view * animator_->modelMatrix(i);
            shader_.setMat4("uMVP", mvp);
            glDrawArrays(GL_TRIANGLES, cubies_[i].hintOffset, cubies_[i].hintCount);
        }
    }

    // ── Pass 2: Foundation (translucent body, normal back-face cull) ───────
    // Blend must be enabled here regardless of showHint_, because the
    // foundation vertices carry alpha (e.g. 0.3 in crystal mode). Without
    // blend the GPU ignores alpha and renders solid black.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glCullFace(GL_BACK);
    for (int i = 0; i < (int)cubies_.size(); ++i) {
        glm::mat4 mvp = proj * view * animator_->modelMatrix(i);
        shader_.setMat4("uMVP", mvp);
        glDrawArrays(GL_TRIANGLES, cubies_[i].foundOffset, cubies_[i].foundCount);
    }

    // ── Pass 3: Opaque stickers (FrontSide ≡ cull GL_BACK) ──────────────────
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glCullFace(GL_BACK);

    for (int i = 0; i < (int)cubies_.size(); ++i) {
        if (cubies_[i].stickerCount == 0) continue;
        glm::mat4 mvp = proj * view * animator_->modelMatrix(i);
        shader_.setMat4("uMVP", mvp);
        glDrawArrays(GL_TRIANGLES, cubies_[i].stickerOffset, cubies_[i].stickerCount);
    }

    glBindVertexArray(0);
    shader_.unbind();
}

void CubeRenderer::destroy() {
    if (!initialized_) return;
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    animator_.reset();
    initialized_ = false;
}

// ---------------------------------------------------------------------------
void CubeRenderer::onDragBegin(float x, float y) {
    dragging_  = true;
    lastDragX_ = x; lastDragY_ = y;
    camera_.beginDrag(nowSeconds() - startTime_);
}

void CubeRenderer::onDragMove(float x, float y) {
    if (!dragging_) return;
    camera_.drag(x - lastDragX_, y - lastDragY_);
    lastDragX_ = x; lastDragY_ = y;
}

void CubeRenderer::onDragEnd() {
    dragging_ = false;
    camera_.endDrag(nowSeconds() - startTime_);
}

void CubeRenderer::onDrag(float dx, float dy) { camera_.drag(dx, dy); }
void CubeRenderer::onZoom(float delta)         { camera_.zoom(delta);  }

void CubeRenderer::applyMove(const twizzle::Move& move) {
    cube_.applyMove(move);
    animator_->queueMove(move);
}

void CubeRenderer::applyAlgorithm(const std::string& algStr) {
    twizzle::Algorithm alg(algStr);
    for (const auto& m : alg.moves()) applyMove(m);
}

void CubeRenderer::reset() {
    cube_.reset();
    animator_->clear();
}

void CubeRenderer::play()         { animator_->play(); }
void CubeRenderer::pause()        { animator_->pause(); }
void CubeRenderer::togglePlay()   { animator_->togglePlay(); }
void CubeRenderer::jumpToStart()  { animator_->jumpToStart(); }
void CubeRenderer::jumpToEnd()    { animator_->jumpToEnd(); }
void CubeRenderer::stepForward()  { animator_->stepForward(); }
void CubeRenderer::stepBackward() { animator_->stepBackward(); }

void  CubeRenderer::seekFraction(float f)  { animator_->seekFraction(f); }
void  CubeRenderer::seekMs(float ms)       { animator_->seekMs(ms); }
float CubeRenderer::currentFraction() const{ return animator_->currentFraction(); }
float CubeRenderer::currentMs()       const{ return animator_->currentMs(); }
float CubeRenderer::totalDurationMs() const{ return animator_->totalDurationMs(); }
bool  CubeRenderer::isPlaying()       const{ return animator_->isPlaying(); }

bool CubeRenderer::isAnimating() const { return animator_ && animator_->isAnimating(); }

void  CubeRenderer::setSpeed(float speed) { if (animator_) animator_->setSpeed(speed); }
float CubeRenderer::getSpeed() const      { return animator_ ? animator_->getSpeed() : 1.0f; }

float CubeRenderer::nowSeconds() {
    using namespace std::chrono;
    return duration<float>(steady_clock::now().time_since_epoch()).count();
}

void CubeRenderer::setBackgroundColor(float r, float g, float b, float a) {
    bgR_ = r; bgG_ = g; bgB_ = b; bgA_ = a;
}

void CubeRenderer::setCameraPosition(float latitude, float longitude, float radius) {
    camera_.setPosition(latitude, longitude, radius);
}

void CubeRenderer::setShowHint(bool show) {
    showHint_ = show;
}

void CubeRenderer::setFaceColors(const float colors[6][3]) {
    glm::vec3 glmColors[6];
    for (int i = 0; i < 6; ++i) {
        glmColors[i] = glm::vec3(colors[i][0], colors[i][1], colors[i][2]);
    }
    CubeGeometry::setFaceColors(glmColors);
    CubeGeometry::build(vertices_, cubies_);
    uploadGeometry();
}

void CubeRenderer::setBodyAlpha(float alpha) {
    CubeGeometry::setBodyAlpha(alpha);
    CubeGeometry::build(vertices_, cubies_);
    uploadGeometry();
}

} // namespace twizzle::renderer
