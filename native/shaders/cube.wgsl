// =============================================================================
// cube.wgsl — Twizzle Cube Shader (WebGPU Shading Language)
//
// Flat-shaded vertex + fragment program for the 3x3x3 Rubik's Cube renderer.
// Matches the visual output of cubing.js's MeshBasicMaterial pipeline:
//   - No lighting / no normals (pure vertex colour passthrough).
//   - Alpha blending enabled (foundation body + hint stickers are semi-transparent).
//   - Per-cubie model matrix supplied via a push-constant / dynamic offset
//     uniform, so the entire cube is drawn in a single pipeline with 26
//     draw calls (one per cubie).
//
// WGSL is compiled at runtime by wgpu-native to:
//   SPIR-V   → Vulkan (Android & Linux)
//   MSL      → Metal (iOS & macOS)
//   HLSL     → DirectX 12 (Windows)
// =============================================================================

// ---------------------------------------------------------------------------
// Bind group 0 — frame-level uniforms (set once per render pass)
// ---------------------------------------------------------------------------
struct FrameUniforms {
    /// View-Projection matrix (same for all cubies in a frame).
    vp : mat4x4<f32>,
    /// Global background / clear colour (not used in shader, kept for alignment).
    bg : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> frame : FrameUniforms;

// ---------------------------------------------------------------------------
// Bind group 1 — per-cubie uniforms (updated once per draw call)
// ---------------------------------------------------------------------------
struct CubieUniforms {
    /// Per-cubie model matrix (rotated by MoveAnimator during animations).
    model  : mat4x4<f32>,
    /// params.x = Foundation body opacity (0.3 = crystal, 1.0 = normal).
    params : vec4<f32>,
};

@group(1) @binding(0)
var<uniform> cubie : CubieUniforms;

// ---------------------------------------------------------------------------
// Vertex input / output
// ---------------------------------------------------------------------------
struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) color    : vec4<f32>,
};

struct VertexOutput {
    @builtin(position) clipPosition : vec4<f32>,
    @location(0)       color        : vec4<f32>,
};

// ---------------------------------------------------------------------------
// Vertex shader
// ---------------------------------------------------------------------------
@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    // MVP = VP * Model (per-cubie animated model matrix)
    let worldPos   = cubie.model * vec4<f32>(in.position, 1.0);
    out.clipPosition = frame.vp * worldPos;

    // Pass colour through, overriding alpha for foundation quads.
    // The C++ side already embeds bodyAlpha in the vertex colour for
    // foundation geometry; here we honour that directly.
    out.color = in.color;

    return out;
}

// ---------------------------------------------------------------------------
// Fragment shader
// ---------------------------------------------------------------------------
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Pure passthrough — flat shading with no lighting calculation.
    // Alpha blending is configured in the pipeline descriptor (src=SrcAlpha,
    // dst=OneMinusSrcAlpha, matching standard Porter-Duff over compositing).
    return in.color;
}
