/// @file CubeGeometry.cpp
/// @brief Builds the static cube mesh — 26 cubies x 3 geometry layers.
/// Ported to native/ — no OpenGL dependency, pure vertex math.
#include "renderer/CubeGeometry.hpp"
#include <cmath>

namespace twizzle::renderer {

// Colours: +X R, -X L, +Y U, -Y D, +Z F, -Z B (from Cube3D.ts axesInfo[]).
glm::vec3 CubeGeometry::kFaceColor[6] = {
    {1.000f, 0.000f, 0.000f},  // R  0xff0000   +X
    {1.000f, 0.600f, 0.000f},  // L  0xff9900   -X
    {1.000f, 1.000f, 1.000f},  // U  0xffffff   +Y
    {1.000f, 1.000f, 0.000f},  // D  0xffff00   -Y
    {0.000f, 1.000f, 0.000f},  // F  0x00ff00   +Z
    {0.133f, 0.400f, 1.000f},  // B  0x2266ff   -Z
};

float CubeGeometry::kFoundAlpha = 1.0f;

void CubeGeometry::setFaceColors(const glm::vec3 colors[6]) {
    for (int i = 0; i < 6; ++i) kFaceColor[i] = colors[i];
}

void CubeGeometry::setBodyAlpha(float alpha) {
    kFoundAlpha = alpha;
}

// ---------------------------------------------------------------------------
// Helper: push a quad — always CCW winding (front face = outward normal).
// ---------------------------------------------------------------------------
static void pushQuad(const glm::vec3 v[4], const glm::vec4& col,
                     std::vector<Vertex>& out) {
    out.push_back({v[0], col}); out.push_back({v[1], col}); out.push_back({v[2], col});
    out.push_back({v[0], col}); out.push_back({v[2], col}); out.push_back({v[3], col});
}

// ---------------------------------------------------------------------------
// appendFoundation — 6-face box [-h,+h]^3 (translucent black)
// ---------------------------------------------------------------------------
void CubeGeometry::appendFoundation(float h, float alpha,
                                    std::vector<Vertex>& v) {
    glm::vec4 col{0.0f, 0.0f, 0.0f, alpha};
    const struct { glm::vec3 verts[4]; } faces[6] = {
        {{{h, h, h},{h,-h, h},{h,-h,-h},{h, h,-h}}},           // +X
        {{{-h, h,-h},{-h,-h,-h},{-h,-h, h},{-h, h, h}}},       // -X
        {{{h, h,-h},{-h, h,-h},{-h, h, h},{h, h, h}}},          // +Y
        {{{h,-h, h},{-h,-h, h},{-h,-h,-h},{h,-h,-h}}},          // -Y
        {{{-h, h, h},{-h,-h, h},{h,-h, h},{h, h, h}}},          // +Z
        {{{h, h,-h},{h,-h,-h},{-h,-h,-h},{-h, h,-h}}},          // -Z
    };
    for (auto& f : faces) pushQuad(f.verts, col, v);
}

// ---------------------------------------------------------------------------
// appendQuad — one sticker/hint quad on a given local face.
// faceIdx: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
// ---------------------------------------------------------------------------
void CubeGeometry::appendQuad(int faceIdx, float elev, float half,
                              const glm::vec4& color,
                              std::vector<Vertex>& v) {
    float s = elev, w = half;
    glm::vec3 verts[4];

    switch (faceIdx) {
    case 0: verts[0]={s, w, w}; verts[1]={s,-w, w}; verts[2]={s,-w,-w}; verts[3]={s, w,-w}; break;
    case 1: verts[0]={-s, w,-w}; verts[1]={-s,-w,-w}; verts[2]={-s,-w, w}; verts[3]={-s, w, w}; break;
    case 2: verts[0]={ w,s,-w}; verts[1]={-w,s,-w}; verts[2]={-w,s, w}; verts[3]={ w,s, w}; break;
    case 3: verts[0]={ w,-s, w}; verts[1]={-w,-s, w}; verts[2]={-w,-s,-w}; verts[3]={ w,-s,-w}; break;
    case 4: verts[0]={-w, w,s}; verts[1]={-w,-w,s}; verts[2]={ w,-w,s}; verts[3]={ w, w,s}; break;
    case 5: verts[0]={ w, w,-s}; verts[1]={ w,-w,-s}; verts[2]={-w,-w,-s}; verts[3]={-w, w,-s}; break;
    default: return;
    }
    pushQuad(verts, color, v);
}

// ---------------------------------------------------------------------------
// buildCubie
// ---------------------------------------------------------------------------
void CubeGeometry::buildCubie(int gx, int gy, int gz,
                              std::vector<Vertex>&    v,
                              CubieInfo&              info) {
    info.gridPos = {gx, gy, gz};

    int gridArr[3] = {gx, gy, gz};
    struct FaceRule { int axis, sign, cIdx; };
    static const FaceRule rules[6] = {
        {0,+1,0},{0,-1,1},{1,+1,2},{1,-1,3},{2,+1,4},{2,-1,5}
    };

    // Hint facelets
    info.hintOffset = (int)v.size();
    for (int fi = 0; fi < 6; ++fi) {
        const auto& r = rules[fi];
        if (gridArr[r.axis] == r.sign) {
            glm::vec4 col{kFaceColor[r.cIdx], kHintAlpha};
            appendQuad(fi, kHintElev, kStickerHalf, col, v);
        }
    }
    info.hintCount = (int)v.size() - info.hintOffset;

    // Foundation
    info.foundOffset = (int)v.size();
    appendFoundation(kFoundHalf, kFoundAlpha, v);
    info.foundCount = (int)v.size() - info.foundOffset;

    // Stickers
    info.stickerOffset = (int)v.size();
    for (int fi = 0; fi < 6; ++fi) {
        const auto& r = rules[fi];
        if (gridArr[r.axis] == r.sign) {
            glm::vec4 col{kFaceColor[r.cIdx], 1.0f};
            appendQuad(fi, kStickerElev, kStickerHalf, col, v);
        }
    }
    info.stickerCount = (int)v.size() - info.stickerOffset;
}

// ---------------------------------------------------------------------------
// Face coordinates to 3D world space mapping (natural upright orientation)
// ---------------------------------------------------------------------------
static glm::vec3 faceToWorld(int faceIdx, float elev, float u, float v) {
    switch (faceIdx) {
    case 0: return {+elev, v, -u}; // +X (Right):  u -> -Z, v -> +Y
    case 1: return {-elev, v,  u}; // -X (Left):   u -> +Z, v -> +Y
    case 2: return { u, +elev, -v}; // +Y (Up):     u -> +X, v -> -Z
    case 3: return { u, -elev,  v}; // -Y (Down):   u -> +X, v -> +Z
    case 4: return { u, v, +elev}; // +Z (Front):  u -> +X, v -> +Y
    case 5: return {-u, v, -elev}; // -Z (Back):   u -> -X, v -> +Y
    default: return {0.0f, 0.0f, 0.0f};
    }
}

static void appendFaceRect2D(int faceIdx, float elev,
                             float u0, float vMin, float u1, float vMax,
                             const glm::vec4& color,
                             std::vector<Vertex>& out) {
    glm::vec3 vert0 = faceToWorld(faceIdx, elev, u0, vMin);
    glm::vec3 vert1 = faceToWorld(faceIdx, elev, u1, vMin);
    glm::vec3 vert2 = faceToWorld(faceIdx, elev, u1, vMax);
    glm::vec3 vert3 = faceToWorld(faceIdx, elev, u0, vMax);

    out.push_back(Vertex{vert0, color}); out.push_back(Vertex{vert1, color}); out.push_back(Vertex{vert2, color});
    out.push_back(Vertex{vert0, color}); out.push_back(Vertex{vert2, color}); out.push_back(Vertex{vert3, color});
}

static void appendFaceQuad2D(int faceIdx, float elev,
                             const glm::vec2& p0, const glm::vec2& p1,
                             const glm::vec2& p2, const glm::vec2& p3,
                             const glm::vec4& color,
                             std::vector<Vertex>& out) {
    glm::vec3 vert0 = faceToWorld(faceIdx, elev, p0.x, p0.y);
    glm::vec3 vert1 = faceToWorld(faceIdx, elev, p1.x, p1.y);
    glm::vec3 vert2 = faceToWorld(faceIdx, elev, p2.x, p2.y);
    glm::vec3 vert3 = faceToWorld(faceIdx, elev, p3.x, p3.y);

    out.push_back(Vertex{vert0, color}); out.push_back(Vertex{vert1, color}); out.push_back(Vertex{vert2, color});
    out.push_back(Vertex{vert0, color}); out.push_back(Vertex{vert2, color}); out.push_back(Vertex{vert3, color});
}

// ---------------------------------------------------------------------------
// buildFaceLetters — fixed face letters (U, D, L, R, F, B)
// ---------------------------------------------------------------------------
void CubeGeometry::buildFaceLetters(std::vector<Vertex>& vertices,
                                   int& letterOffset,
                                   int& letterCount) {
    letterOffset = (int)vertices.size();

    // 1.0 (center cubie offset) + kStickerElev (0.503) + 0.002 = 1.505f
    const float elev = 1.0f + kStickerElev + 0.002f;
    const float w = 0.13f;
    const float h = 0.18f;
    const float t = 0.045f;

    // Face index mapping:
    // 0: +X -> 'R'
    // 1: -X -> 'L'
    // 2: +Y -> 'U'
    // 3: -Y -> 'D'
    // 4: +Z -> 'F'
    // 5: -Z -> 'B'
    for (int fi = 0; fi < 6; ++fi) {
        const glm::vec3& fc = kFaceColor[fi];
        float lum = 0.299f * fc.r + 0.587f * fc.g + 0.114f * fc.b;
        glm::vec4 col = (lum > 0.55f)
            ? glm::vec4(0.08f, 0.08f, 0.08f, 1.0f) // Dark charcoal on light faces
            : glm::vec4(1.00f, 1.00f, 1.00f, 1.0f); // Pure white on dark faces

        switch (fi) {
        case 0: // 'R' (+X)
            // Left vertical bar
            appendFaceRect2D(fi, elev, -w, -h, -w + t, h, col, vertices);
            // Top bar
            appendFaceRect2D(fi, elev, -w + t, h - t, w - t * 1.2f, h, col, vertices);
            // Mid bar
            appendFaceRect2D(fi, elev, -w + t, 0.0f, w - t * 1.2f, t, col, vertices);
            // Upper right bar
            appendFaceRect2D(fi, elev, w - t, t * 0.8f, w, h - t * 0.8f, col, vertices);
            // Top-right corner chamfer
            appendFaceQuad2D(fi, elev,
                {w - t * 1.2f, h - t}, {w - t * 1.2f, h},
                {w, h - t * 0.8f}, {w - t, h - t * 0.8f},
                col, vertices);
            // Mid-right corner chamfer
            appendFaceQuad2D(fi, elev,
                {w - t * 1.2f, 0.0f}, {w - t * 1.2f, t},
                {w, t * 0.8f}, {w - t, t * 0.8f},
                col, vertices);
            // Diagonal leg
            appendFaceQuad2D(fi, elev,
                {w - t, -h}, {w, -h},
                {-w + t * 2.2f, 0.0f}, {-w + t * 1.2f, 0.0f},
                col, vertices);
            break;

        case 1: // 'L' (-X)
            // Left vertical bar
            appendFaceRect2D(fi, elev, -w, -h, -w + t, h, col, vertices);
            // Bottom bar
            appendFaceRect2D(fi, elev, -w, -h, w, -h + t, col, vertices);
            break;

        case 2: // 'U' (+Y)
            // Left vertical bar
            appendFaceRect2D(fi, elev, -w, -h + t, -w + t, h, col, vertices);
            // Right vertical bar
            appendFaceRect2D(fi, elev, w - t, -h + t, w, h, col, vertices);
            // Bottom horizontal bar
            appendFaceRect2D(fi, elev, -w, -h, w, -h + t, col, vertices);
            break;

        case 3: // 'D' (-Y)
            // Left vertical bar
            appendFaceRect2D(fi, elev, -w, -h, -w + t, h, col, vertices);
            // Top horizontal bar
            appendFaceRect2D(fi, elev, -w + t, h - t, w - t * 1.5f, h, col, vertices);
            // Bottom horizontal bar
            appendFaceRect2D(fi, elev, -w + t, -h, w - t * 1.5f, -h + t, col, vertices);
            // Right vertical bar
            appendFaceRect2D(fi, elev, w - t, -h + t * 1.5f, w, h - t * 1.5f, col, vertices);
            // Top-right corner chamfer
            appendFaceQuad2D(fi, elev,
                {w - t * 1.5f, h - t}, {w - t * 1.5f, h},
                {w, h - t * 1.5f}, {w - t, h - t * 1.5f},
                col, vertices);
            // Bottom-right corner chamfer
            appendFaceQuad2D(fi, elev,
                {w - t * 1.5f, -h}, {w - t * 1.5f, -h + t},
                {w - t, -h + t * 1.5f}, {w, -h + t * 1.5f},
                col, vertices);
            break;

        case 4: // 'F' (+Z)
            // Left vertical bar
            appendFaceRect2D(fi, elev, -w, -h, -w + t, h, col, vertices);
            // Top horizontal bar
            appendFaceRect2D(fi, elev, -w + t, h - t, w, h, col, vertices);
            // Middle horizontal bar
            appendFaceRect2D(fi, elev, -w + t, 0.0f, w * 0.7f, t, col, vertices);
            break;

        case 5: // 'B' (-Z)
            // Left vertical bar
            appendFaceRect2D(fi, elev, -w, -h, -w + t, h, col, vertices);
            // Top bar
            appendFaceRect2D(fi, elev, -w + t, h - t, w - t * 1.2f, h, col, vertices);
            // Mid bar
            appendFaceRect2D(fi, elev, -w + t, -t * 0.5f, w - t * 1.2f, t * 0.5f, col, vertices);
            // Bottom bar
            appendFaceRect2D(fi, elev, -w + t, -h, w - t * 1.2f, -h + t, col, vertices);
            // Upper right vertical bar
            appendFaceRect2D(fi, elev, w - t, t * 0.5f, w, h - t * 0.8f, col, vertices);
            // Lower right vertical bar
            appendFaceRect2D(fi, elev, w - t, -h + t * 0.8f, w, -t * 0.5f, col, vertices);
            // Top-right chamfer
            appendFaceQuad2D(fi, elev,
                {w - t * 1.2f, h - t}, {w - t * 1.2f, h},
                {w, h - t * 0.8f}, {w - t, h - t * 0.8f},
                col, vertices);
            // Bottom-right chamfer
            appendFaceQuad2D(fi, elev,
                {w - t * 1.2f, -h}, {w - t * 1.2f, -h + t},
                {w - t, -h + t * 0.8f}, {w, -h + t * 0.8f},
                col, vertices);
            break;
        }
    }

    letterCount = (int)vertices.size() - letterOffset;
}

// ---------------------------------------------------------------------------
// build — all 26 cubies
// ---------------------------------------------------------------------------
void CubeGeometry::build(std::vector<Vertex>&    vertices,
                          std::vector<CubieInfo>& cubies) {
    vertices.clear(); cubies.clear();
    vertices.reserve(26 * 120 + 300); cubies.reserve(26);

    for (int gx = -1; gx <= 1; ++gx)
    for (int gy = -1; gy <= 1; ++gy)
    for (int gz = -1; gz <= 1; ++gz) {
        if (gx==0 && gy==0 && gz==0) continue;
        CubieInfo info;
        buildCubie(gx, gy, gz, vertices, info);
        cubies.push_back(info);
    }
}

} // namespace twizzle::renderer
