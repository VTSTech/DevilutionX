// Negative control: reads PassOutput0 from pass 0 — impossible (a pass
// cannot sample its own output before it exists). The loader MUST reject
// this preset with "slang: PassOutput0 is not causal in pass 0" and turn
// the Shader Filter off for the session (failure contract).
#pragma name feedback_noncausal

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
uniform sampler2D PassOutput0;

void main()
{
	vec3 now = texture(Source, vTexCoord).rgb;
	vec3 self = texture(PassOutput0, vTexCoord).rgb;
	FragColor = vec4(mix(now, self, 0.5), 1.0);
}
