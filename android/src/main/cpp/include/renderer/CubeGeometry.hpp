#pragma once
#include "gl_platform.hpp"
#include <vector>

namespace twizzle::renderer {

/// One vertex: position + rgba colour. No normals — flat MeshBasicMaterial
/// style shading, matching cubing.js (no lighting computation needed).
struct Vertex {
    glm::vec3 position;
    glm::vec4 color;
};

/// Per-cubie metadata: its solved-state grid slot and the three vertex
/// sub-ranges (hint / foundation / sticker) it owns inside the shared VBO.
struct CubieInfo {
    glm::ivec3 gridPos; ///< solved-state position, components in {-1,0,1}

    int hintOffset,    hintCount;    ///< hint facelets (transparent, BackSide)
    int foundOffset,   foundCount;   ///< black foundation body (semi-transparent)
    int stickerOffset, stickerCount; ///< coloured stickers (opaque)
};

/**
 * @brief Generates 3x3x3 cube vertex data, faithful to cubing.js's Cube3D.ts.
 *
 * Builds 26 cubies (the centre is omitted — never visible), each with three
 * geometry layers: a translucent black foundation box, opaque front-facing
 * stickers, and back-facing "hint" stickers that only become visible when
 * the real sticker's outward face points away from the camera.
 *
 * Dimensions/colours are taken directly from cubing.js constants:
 *  - foundation: full 1x1x1 box (cubies touch exactly, no physical gap)
 *  - sticker elevation 0.503, scale 0.85 of a 1x1 base quad
 *  - hint elevation 1.45 (floating ghost sticker, alpha 0.5)
 */
class CubeGeometry {
public:
    static constexpr float kFoundHalf   = 0.500f;          ///< foundation box half-size
    static constexpr float kStickerElev = 0.503f;          ///< sticker distance from cubie centre
    static constexpr float kStickerHalf = 0.500f * 0.85f;  ///< sticker half-size (= 0.425)
    static constexpr float kHintElev    = 1.45f;            ///< hint sticker distance from centre
    static constexpr float kFoundAlpha  = 0.30f;            ///< foundation opacity
    static constexpr float kHintAlpha   = 0.50f;            ///< hint sticker opacity

    /// Sticker colours indexed by face: +X, -X, +Y, -Y, +Z, -Z.
    static const glm::vec3 kFaceColor[6];

    /// @brief Builds all 26 cubies into flat vertex + metadata buffers.
    /// @param vertices Cleared and filled with every triangle vertex.
    /// @param cubies   Cleared and filled with one CubieInfo per cubie.
    static void build(std::vector<Vertex>&    vertices,
                      std::vector<CubieInfo>& cubies);

private:
    /// Builds a single cubie at solved-state grid position (gx,gy,gz).
    static void buildCubie(int gx, int gy, int gz,
                           std::vector<Vertex>&    v,
                           CubieInfo&              info);

    /// Appends a translucent black 1x1x1 box centred at the origin.
    static void appendFoundation(float h, float alpha,
                                 std::vector<Vertex>& v);

    /// Appends one sticker/hint quad on a given local face.
    /// @param faceIdx 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    /// @param elev    distance from the cubie centre along the face normal
    /// @param half    half-width of the quad
    static void appendQuad(int faceIdx, float elev, float half,
                           const glm::vec4& color,
                           std::vector<Vertex>& v);
};

} // namespace twizzle::renderer
