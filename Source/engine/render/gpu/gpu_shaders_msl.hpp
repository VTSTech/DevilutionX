// DevilutionX GPU scaler - Metal Shading Language sources for the Metal
// backend (macOS). Compiled at runtime by SDL from MSL source.
//
// Binding conventions follow SDL_CreateGPUShader for MSL fragment shaders:
//   textures: [[texture(0)]..], samplers: [[sampler(0)]..]
//   uniform buffers: first in the [[buffer]] argument table by SDL slot
//
// The built-in ScaleFX/HQx filters (and their MSL/GLSL ports) were removed;
// the Shader Filter option loads libretro slang presets instead, so only the
// shared fullscreen-vertex and blit-fragment programs remain embedded here.
//
// FEAT-02 (2026-10-17): unwired. CreateDevice no longer accepts MSL, so this
// header is not included anywhere; it is kept for a future validated Metal
// path (see the deltas.md FEAT-02 closure record). Do not re-include it
// without also re-accepting Metal at the device layer.

#pragma once

namespace devilution::gpu::msl {

static constexpr const char *kVertex = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VSOut {
    float4 pos [[position]];
    float2 uv [[user(locn0)]];
};

vertex VSOut fullscreenVS(uint vid [[vertex_id]])
{
    // Fullscreen triangle: (-1,-1), (3,-1), (-1,3) - both axes overscan to 3.
    float2 pos = float2(float((vid & 1u) << 2) - 1.0, float((vid & 2u) << 1) - 1.0);
    VSOut o;
    o.uv = (pos + 1.0) * 0.5;
    o.pos = float4(pos, 0.0, 1.0);
    return o;
}
)MSL";
static constexpr const char *kBlit = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FragUniform {
    float4 uSourceSize;
    float4 uParams;
};

fragment float4 blitPS(float2 vUV [[user(locn0)]],
    texture2d<float> uTex [[texture(0)]],
    sampler uSamp [[sampler(0)]],
    constant FragUniform &u [[buffer(0)]])
{
    return uTex.sample(uSamp, vUV);
}
)MSL";

} // namespace devilution::gpu::msl
