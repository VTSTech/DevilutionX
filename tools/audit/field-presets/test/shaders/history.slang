// F5 history-ring canary: taps the history ring at a GAP index
// (OriginalHistory1 and OriginalHistory3, skipping 2) — the ring-depth
// worst case that motivated the F5 fix.
#pragma name history_ring

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
layout(std140, set = 0, binding = 0) uniform UBO
{
	mat4 MVP;
} global;
void main() {
	gl_Position = global.MVP * Position;
	vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(std140, set = 0, binding = 1) uniform UBO
{
	vec4 SourceSize;
	vec4 OriginalSize;
	vec4 OutputSize;
	uint FrameCount;
} params;

uniform sampler2D Source;
uniform sampler2D OriginalHistory1;
uniform sampler2D OriginalHistory3;

void main()
{
	vec3 c0 = texture(Source, vTexCoord).rgb;
	vec3 c1 = texture(OriginalHistory1, vTexCoord).rgb;
	vec3 c3 = texture(OriginalHistory3, vTexCoord).rgb;
	FragColor = vec4((c0 + c1 + c3) / 3.0, 1.0);
}
