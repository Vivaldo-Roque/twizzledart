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
// build — all 26 cubies
// ---------------------------------------------------------------------------
void CubeGeometry::build(std::vector<Vertex>&    vertices,
                          std::vector<CubieInfo>& cubies) {
    vertices.clear(); cubies.clear();
    vertices.reserve(26 * 120); cubies.reserve(26);

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
