/**
 * Hostile-input regression tests for the preset parser / slang translator
 * (closes ROB-01 + ROB-02; registered as the `preset_failure_modes` ctest).
 * Headless: exercises ParsePreset + TranslateSlang only (no GPU, no SDL link).
 *
 * Every case that previously terminated the process through an uncaught
 * std::stoi / std::stof / std::bad_alloc must now either fail politely with
 * an error string or succeed with sane fallback values.
 */
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/render/gpu/preset/preset_parser.hpp"
#include "engine/render/gpu/preset/slang_translate.hpp"

namespace preset = devilution::gpu_scaler::preset;

// Harness stub: preset_parser calls SDL_GetBasePath for the shader root dir.
// SDL is deliberately NOT linked into this test target (it must stay runnable
// on any headless CI box); path resolution itself is not under test.
extern "C" char *SDL_GetBasePath(void)
{
	static char path[] = "/";
	return path;
}

namespace {

int gFailures = 0;
int gChecks = 0;

const std::string kWorkRoot = (std::filesystem::temp_directory_path() / "devilutionx_preset_failure_modes").string();
const std::string kPresetDir = kWorkRoot + "/presets";
const std::string kShaderDir = kWorkRoot + "/shaders";

void WriteFile(const std::string &path, const std::string &content)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << content;
}

void Check(bool condition, const std::string &what)
{
	++gChecks;
	if (condition) {
		std::printf("  PASS: %s\n", what.c_str());
	} else {
		++gFailures;
		std::printf("  FAIL: %s\n", what.c_str());
	}
}

// ---------------------------------------------------------------- presets --

void TestHostileDeclaredCount()
{
	std::printf("[ROB-02] shaders = 2147483647 (the ~250 GB allocation case)\n");
	WriteFile(kPresetDir + "/hostile_count.slangp", "shaders = 2147483647\n");
	preset::Preset out;
	std::string error;
	const bool ok = preset::ParsePreset(kPresetDir + "/hostile_count.slangp", out, error);
	Check(!ok, "preset fails to load");
	Check(error.find("no shader passes") != std::string::npos,
	    "error names the real problem: " + error);
}

void TestHostilePassIndex()
{
	std::printf("[ROB-02] shader2147483647 = x (in-int-range absurd index)\n");
	WriteFile(kPresetDir + "/hostile_index.slangp",
	    "shader2147483647 = shaders/simple.slang\n");
	preset::Preset out;
	std::string error;
	const bool ok = preset::ParsePreset(kPresetDir + "/hostile_index.slangp", out, error);
	Check(!ok, "preset fails to load");
	Check(error.find("exceeds the limit of 32") != std::string::npos,
	    "error names the limit: " + error);
}

void TestDeclaredCountIgnored()
{
	std::printf("[ROB-02] declared count above the real chain is ignored\n");
	WriteFile(kPresetDir + "/count_vs_keys.slangp",
	    "shaders = 2147483647\n"
	    "shader0 = shaders/simple.slang\n"
	    "shader1 = shaders/second.slang\n"
	    "alias1 = blur\n");
	preset::Preset out;
	std::string error;
	const bool ok = preset::ParsePreset(kPresetDir + "/count_vs_keys.slangp", out, error);
	Check(ok, "preset loads: " + error);
	Check(out.passes.size() == 2, "chain has exactly the 2 shaderN passes");
	Check(out.passes[1].alias == "blur", "alias survives");
}

void TestPassIndexBoundary()
{
	std::printf("[ROB-02] pass index boundary (31 ok, 32 rejected)\n");
	WriteFile(kPresetDir + "/edge31.slangp", "shader31 = shaders/simple.slang\n");
	preset::Preset out;
	std::string error;
	Check(preset::ParsePreset(kPresetDir + "/edge31.slangp", out, error),
	    "shader31 (last allowed slot) loads: " + error);
	Check(out.passes.size() == 1 && out.passes[0].shaderPath == "shaders/simple.slang",
	    "chain collapses to the 1 keyed pass");

	WriteFile(kPresetDir + "/edge32.slangp", "shader32 = shaders/simple.slang\n");
	Check(!preset::ParsePreset(kPresetDir + "/edge32.slangp", out, error),
	    "shader32 (first over the limit) fails");
}

void TestValidPresetUnchanged()
{
	std::printf("[ROB-02] valid preset parses identically (regression guard)\n");
	WriteFile(kPresetDir + "/valid.slangp",
	    "shaders = 2\n"
	    "shader0 = shaders/simple.slang\n"
	    "filter_linear0 = true\n"
	    "scale_type_x0 = viewport\n"
	    "scale_x0 = 1.0\n"
	    "shader1 = shaders/second.slang\n"
	    "alias1 = final\n"
	    "histories = 4\n");
	preset::Preset out;
	std::string error;
	const bool ok = preset::ParsePreset(kPresetDir + "/valid.slangp", out, error);
	Check(ok, "loads: " + error);
	Check(out.passes.size() == 2, "2 passes");
	Check(out.passes[0].filter == preset::FilterMode::Linear, "filter_linear0 honored");
	Check(out.histories == 4, "histories honored");
}

// -------------------------------------------------------------- translation --

void TestMalformedPragmaNumbers()
{
	std::printf("[ROB-01] #pragma parameter with \".\" and \"-\" values\n");
	// Pre-fix: std::stof(".") threw std::invalid_argument and terminated
	// translation. Post-fix: strtof contract, malformed -> 0 (RetroArch/atof
	// semantics).
	WriteFile(kShaderDir + "/pragma_edge.slang",
	    "#pragma parameter densy \"Density\" . - 1.0\n"
	    "#pragma parameter densx \"DensityX\" - . 1.0\n"
	    "#pragma stage vertex\n"
	    "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n"
	    "#pragma stage fragment\n"
	    "uniform sampler2D Source;\n"
	    "void main() { gl_FragColor = texture(Source, vec2(0.5)); }\n");
	preset::SlangTranslation out;
	const bool ok = preset::TranslateSlang(kShaderDir + "/pragma_edge.slang", {}, 0, 1, out);
	Check(ok, "translation succeeds: " + out.error);
	Check(out.parameters.size() == 2, "both pragma parameters extracted");
	if (out.parameters.size() == 2) {
		Check(out.parameters[0].defaultValue == 0.0F && out.parameters[0].minimum == 0.0F,
		    "malformed default/minimum parse as 0 (atof semantics)");
	}
}

void TestHugeHistoryIndex()
{
	std::printf("[ROB-01] OriginalHistory99999999999999 sampler (20-digit index)\n");
	// Pre-fix: std::stoi on the regex-validated digit string threw
	// std::out_of_range and terminated translation.
	WriteFile(kShaderDir + "/huge_history.slang",
	    "#pragma stage vertex\n"
	    "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n"
	    "#pragma stage fragment\n"
	    "uniform sampler2D OriginalHistory99999999999999;\n"
	    "void main() { gl_FragColor = texture(OriginalHistory99999999999999, vec2(0.5)); }\n");
	preset::SlangTranslation out;
	const bool ok = preset::TranslateSlang(kShaderDir + "/huge_history.slang", {}, 0, 1, out);
	Check(!ok, "translation fails cleanly");
	Check(out.error.find("out of range") != std::string::npos,
	    "error explains the range: " + out.error);
}

void TestBuiltinNamesStillValid()
{
	std::printf("[ROB-01] legitimate builtin names still accepted (regression guard)\n");
	WriteFile(kShaderDir + "/builtins.slang",
	    "#pragma stage vertex\n"
	    "void main() { gl_Position = vec4(0.0); }\n"
	    "#pragma stage fragment\n"
	    "uniform sampler2D Source;\n"
	    "uniform sampler2D Original;\n"
	    "uniform sampler2D OriginalHistory2;\n"
	    "uniform sampler2D PassOutput0;\n"
	    "uniform sampler2D User0;\n"
	    "void main() { gl_FragColor = texture(Source, vec2(0.5)) + texture(Original, vec2(0.1))\n"
	    "    + texture(OriginalHistory2, vec2(0.2)) + texture(PassOutput0, vec2(0.3))\n"
	    "    + texture(User0, vec2(0.4)); }\n");
	preset::SlangTranslation out;
	// PassOutput0 is only causal from pass 1 on (pre-existing translator rule).
	const bool ok = preset::TranslateSlang(kShaderDir + "/builtins.slang", {}, 1, 2, out);
	Check(ok, "translation succeeds: " + out.error);
}

} // namespace

int main()
{
	std::filesystem::create_directories(kPresetDir);
	std::filesystem::create_directories(kShaderDir);
	WriteFile(kShaderDir + "/simple.slang",
	    "#pragma stage vertex\n"
	    "void main() { gl_Position = vec4(0.0); }\n"
	    "#pragma stage fragment\n"
	    "void main() { }\n");
	// second.slang is never translated; the parser only stores paths.

	TestHostileDeclaredCount();
	TestHostilePassIndex();
	TestDeclaredCountIgnored();
	TestPassIndexBoundary();
	TestValidPresetUnchanged();
	TestMalformedPragmaNumbers();
	TestHugeHistoryIndex();
	TestBuiltinNamesStillValid();

	std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
