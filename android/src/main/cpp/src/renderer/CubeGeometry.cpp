/// @file CubeGeometry.cpp
/// @brief Builds the static cube mesh — 26 cubies x 3 geometry layers.
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

float CubeGeometry::kFoundAlpha = 0.30f;

void CubeGeometry::setFaceColors(const glm::vec3 colors[6]) {
    for (int i = 0; i < 6; ++i) {
        kFaceColor[i] = colors[i];
    }
}

void CubeGeometry::setBodyAlpha(float alpha) {
    kFoundAlpha = alpha;
}

// ---------------------------------------------------------------------------
// Helper: push a quad — always CCW winding (front face = outward normal).
// Both the regular sticker AND the hint sticker use the SAME winding/shape;
// the only difference is which side gets culled at render time
// (GL_BACK for regular stickers = three.js FrontSide,
//  GL_FRONT for hint stickers   = three.js BackSide).
// This is the exact mechanism used by cubing.js: same geometry, different
// `side` material property — never a geometric/winding hack.
// ---------------------------------------------------------------------------
static void pushQuad(const glm::vec3 v[4], const glm::vec4& col,
                     std::vector<Vertex>& out) {
    out.push_back({v[0], col}); out.push_back({v[1], col}); out.push_back({v[2], col});
    out.push_back({v[0], col}); out.push_back({v[2], col}); out.push_back({v[3], col});
}

// ---------------------------------------------------------------------------
// appendFoundation — 6-face box  [-h,+h]^3  (translucent black, alpha=0.3)
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
// appendQuad — one sticker quad on a face (used for BOTH regular & hint;
// winding is identical, normal points outward in all cases).
// faceIdx: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
// elev   : distance from cubie centre to the quad along its normal
// half   : half-width of the quad
// ---------------------------------------------------------------------------
void CubeGeometry::appendQuad(int faceIdx, float elev, float half,
                              const glm::vec4& color,
                              std::vector<Vertex>& v) {
    float s = elev;
    float w = half;
    glm::vec3 verts[4];

    switch (faceIdx) {
    case 0: // +X   normal (1,0,0)
        verts[0]={s, w, w}; verts[1]={s,-w, w}; verts[2]={s,-w,-w}; verts[3]={s, w,-w};
        break;
    case 1: // -X
        verts[0]={-s, w,-w}; verts[1]={-s,-w,-w}; verts[2]={-s,-w, w}; verts[3]={-s, w, w};
        break;
    case 2: // +Y
        verts[0]={ w,s,-w}; verts[1]={-w,s,-w}; verts[2]={-w,s, w}; verts[3]={ w,s, w};
        break;
    case 3: // -Y
        verts[0]={ w,-s, w}; verts[1]={-w,-s, w}; verts[2]={-w,-s,-w}; verts[3]={ w,-s,-w};
        break;
    case 4: // +Z
        verts[0]={-w, w,s}; verts[1]={-w,-w,s}; verts[2]={ w,-w,s}; verts[3]={ w, w,s};
        break;
    case 5: // -Z
        verts[0]={ w, w,-s}; verts[1]={ w,-w,-s}; verts[2]={-w,-w,-s}; verts[3]={-w, w,-s};
        break;
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
    // face rule: axis, sign, colorIdx (+X=0,-X=1,+Y=2,-Y=3,+Z=4,-Z=5)
    struct FaceRule { int axis, sign, cIdx; };
    static const FaceRule rules[6] = {
        {0,+1,0},{0,-1,1},{1,+1,2},{1,-1,3},{2,+1,4},{2,-1,5}
    };

    // ── Hint facelets — SAME shape/winding as regular sticker, just at a
    //    larger elevation (1.45). Visibility is controlled purely by
    //    face-culling mode at render time (cull GL_FRONT → BackSide),
    //    so they are only ever seen when the real sticker's outward face
    //    points AWAY from the camera. ──────────────────────────────────
    info.hintOffset = (int)v.size();
    for (int fi = 0; fi < 6; ++fi) {
        const auto& r = rules[fi];
        if (gridArr[r.axis] == r.sign) {
            glm::vec4 col{kFaceColor[r.cIdx], kHintAlpha};
            appendQuad(fi, kHintElev, kStickerHalf, col, v);
        }
    }
    info.hintCount = (int)v.size() - info.hintOffset;

    // ── Foundation (semi-transparent black box) ───────────────────────────
    info.foundOffset = (int)v.size();
    appendFoundation(kFoundHalf, kFoundAlpha, v);
    info.foundCount = (int)v.size() - info.foundOffset;

    // ── Stickers (opaque, cull GL_BACK → FrontSide, elevation 0.503) ──────
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
