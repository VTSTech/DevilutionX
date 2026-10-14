// Round-4 canary: cross-stage-region re-include (koko-aio pattern).
//
// r4_functions.inc is included under #pragma stage vertex AND again under
// #pragma stage fragment. The spec's #include is plain textual inclusion, so
// each stage's translation unit (shared + that stage region) needs its own
// copy. A file-lifetime include-once dropped the fragment copy and the field
// failed with "ERROR: 'hsv2rgb' : no matching overloaded function found"
// (koko-aio-ng pass 0, colortools_and_ntsc_pass.slang line 1749).
//
// The second fragment-region include of r4_functions.inc is a same-bucket
// repeat: include-once per stage bucket must still dedupe it.
//
// r4VertexOnly/r4FragmentOnly are stage-boundary markers: if the #pragma
// stage lines ever disappear from the merged text, all content lands in the
// shared bucket and these leak into the opposite stage's GLSL.
#pragma name r4_include

#include "r4_cycle_a.inc"

#pragma stage vertex
#include "r4_functions.inc"
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
layout(std140, set = 0, binding = 0) uniform UBO
{
        mat4 MVP;
} global;
vec4 r4VertexOnly() { return vec4(1.0); }
void main() {
        gl_Position = global.MVP * Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
uniform sampler2D Source;
vec4 r4FragmentOnly() { return vec4(0.5); }
#include "r4_functions.inc"
#include "r4_functions.inc"

void main()
{
        FragColor = r4Tint(texture(Source, vTexCoord)) + r4FragmentOnly() * 0.0;
}
