// slang_compile.cpp — glslang frontend glue.
//
// Ported from the reference host (VTSTech/DevilutionX,
// Source/engine/render/gpu/preset/slang_compile.cpp @ origin/master 96af9b5fc).
// Divergences from the original (see PROVENANCE.md):
//   - USE_GPU_SCALER / USE_SDL1 build guards removed (the library always
//     builds this translation unit).
//   - The precompiled-sidecar fast path and the content-addressed disk cache
//     (picosha2, cache version salt, atomic temp-file writer) are removed by
//     design: v1 compiles with glslang on every load (plan §2 non-goal).
//   - fork logging replaced by the internal log callback shim (compat.hpp).

#include "slangvk/core/slang_compile.hpp"

#include <cstdint>
#include <cstring>
#include <mutex>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include "compat.hpp"

namespace slv {
namespace preset {
namespace {

/** @brief Process-wide glslang lifecycle guard. */
class GlslangRuntime {
public:
	static GlslangRuntime &Instance()
	{
		static GlslangRuntime runtime;
		return runtime;
	}

	GlslangRuntime()
	{
		glslang::InitializeProcess();
	}
	~GlslangRuntime()
	{
		glslang::FinalizeProcess();
	}
	GlslangRuntime(const GlslangRuntime &) = delete;
	GlslangRuntime &operator=(const GlslangRuntime &) = delete;
};

bool CompileStageToSpv(EShLanguage stage, const std::string &source, std::vector<uint8_t> &out, std::string &error)
{
	GlslangRuntime::Instance();

	glslang::TShader shader(stage);
	const char *sourcePtr = source.c_str();
	shader.setStrings(&sourcePtr, 1);
	shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
	shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
	shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
	const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
	if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
		error = StrCat("glslang parse failed (", stage == EShLangVertex ? "vertex" : "fragment", "): ",
		    shader.getInfoLog());
		return false;
	}
	glslang::TProgram program;
	program.addShader(&shader);
	if (!program.link(EShMsgDefault)) {
		error = StrCat("glslang link failed: ", program.getInfoLog());
		return false;
	}
	std::vector<unsigned int> spirv;
	glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
	out.resize(spirv.size() * sizeof(uint32_t));
	std::memcpy(out.data(), spirv.data(), out.size());
	return true;
}

} // namespace

bool CompilePass(const SlangTranslation &translation, PassSpirv &out, std::string &error)
{
	out.vertex.clear();
	out.fragment.clear();

	if (!translation.vertex.present || !translation.fragment.present) {
		error = "slang: pass is missing a required stage";
		return false;
	}

	if (!CompileStageToSpv(EShLangVertex, translation.vertex.glsl, out.vertex, error))
		return false;
	if (!CompileStageToSpv(EShLangFragment, translation.fragment.glsl, out.fragment, error))
		return false;
	return true;
}

} // namespace preset
} // namespace slv
