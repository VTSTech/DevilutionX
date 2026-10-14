// F6 feedback canary: mixes the current frame with this pass's previous
// OUTPUT (PassFeedback0 — double-buffered ping-pong). A working load shows
// a motion trail; the first frames after a preset switch start from black.
#pragma name feedback_mix

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
uniform sampler2D PassFeedback0;

void main()
{
	vec3 now = texture(Source, vTexCoord).rgb;
	vec3 prev = texture(PassFeedback0, vTexCoord).rgb;
	FragColor = vec4(mix(now, prev, 0.5), 1.0);
}
