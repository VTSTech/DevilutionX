#include "slangvk/core/slang_translate.hpp"
#include "slangvk/core/preset_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "compat.hpp"

namespace slv {
namespace preset {
namespace {

std::string TrimAll(const std::string &s)
{
        size_t begin = 0;
        size_t end = s.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0)
                ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)
                --end;
        return s.substr(begin, end - begin);
}

/** @brief Strips line and block comments from a copy of the text (for structural parsing). */
std::string StripComments(const std::string &text)
{
        std::string out;
        out.reserve(text.size());
        bool inLineComment = false;
        bool inBlockComment = false;
        for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                const char next = i + 1 < text.size() ? text[i + 1] : '\0';
                if (inLineComment) {
                        if (c == '\n') {
                                inLineComment = false;
                                out += c;
                        }
                        continue;
                }
                if (inBlockComment) {
                        if (c == '*' && next == '/') {
                                inBlockComment = false;
                                ++i;
                        } else if (c == '\n') {
                                out += c; // Keep line structure.
                        }
                        continue;
                }
                if (c == '/' && next == '/') {
                        inLineComment = true;
                        ++i;
                        continue;
                }
                if (c == '/' && next == '*') {
                        inBlockComment = true;
                        ++i;
                        continue;
                }
                out += c;
        }
        return out;
}

// Stage-region pragma. ResolveIncludes must walk the stream with the exact
// same matcher SplitStages applies to the merged text so that include-once
// scoping mirrors where each line finally lands (shared/vertex/fragment).
const std::regex kStagePragmaRe(R"(^\s*#pragma\s+stage\s+(vertex|fragment)\s*$)");

// ROB-20: every shader read - the top-level file and every include - is
// capped. The old slurp (ostringstream << rdbuf + str()) had no bound: a
// multi-gigabyte planted file threw std::bad_alloc with no handler anywhere
// on the load path, terminating the process instead of returning the
// documented error. 4 MiB per file is ~2 orders of magnitude above anything
// in the libretro corpus; the merged text stays bounded by per-bucket
// include-once times the per-file cap.
constexpr std::streamoff kMaxShaderFileBytes = 4 * 1024 * 1024;

/** @brief size_t -> int for the diagnostics fields; clamps instead of
 * overflowing int past 2 GiB (ROB-20). */
int DiagSizeToInt(size_t size)
{
        constexpr size_t kIntMax = static_cast<size_t>(std::numeric_limits<int>::max());
        return static_cast<int>(size < kIntMax ? size : kIntMax);
}

/** @brief Resolves #include directives recursively, relative to the including file.
 *
 * The spec's #include is plain textual inclusion; the corpus deliberately
 * re-includes shared function files per stage region (koko-aio's
 * colortools_and_ntsc_pass.slang includes functions.include.slang under
 * #pragma stage vertex AND again under #pragma stage fragment). A
 * file-lifetime visited set dropped the second inclusion, leaving the
 * fragment stage without the functions (field round 4: "'hsv2rgb' : no
 * matching overloaded function found"). Include-once is therefore scoped per
 * stage bucket: each bucket's translation unit is shared + that stage, so a
 * re-include in another region is required, while a repeat within one region
 * would double-paste into a single TU and fail to compile everywhere - once
 * per bucket is exactly the safe set. The active stack additionally guards
 * cross-bucket self-recursion that the per-bucket sets cannot see.
 */
bool ResolveIncludes(const std::string &path, int depth, int &stage,
    std::set<std::string> *visited /* 3 buckets */, std::vector<std::string> &active,
    std::string &out, std::string &error)
{
        constexpr int kMaxDepth = 24;
        if (depth > kMaxDepth) {
                error = StrCat("slang: include depth limit exceeded at ", path);
                return false;
        }
        std::error_code ec;
        const std::string canonical = std::filesystem::weakly_canonical(path, ec).string();
        const std::string &key = canonical.empty() ? path : canonical;
        if (std::find(active.begin(), active.end(), key) != active.end()) {
                // Cycle guard: the file is still being expanded on an outer
                // frame (its body switched stage region in between).
                return true;
        }
        active.push_back(key);
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
                active.pop_back();
                error = StrCat("slang: cannot open shader file: ", path);
                return false;
        }
        // ROB-20: cap the read before slurping; shaders are regular files, so
        // an unseekable stream is refused too.
        stream.seekg(0, std::ios::end);
        const std::streamoff fileBytes = stream.tellg();
        stream.seekg(0, std::ios::beg);
        if (fileBytes < 0 || fileBytes > kMaxShaderFileBytes) {
                active.pop_back();
                error = StrCat("slang: shader file too large: ", path, " (limit is 4 MiB)");
                return false;
        }
        std::ostringstream ss;
        ss << stream.rdbuf();
        std::string content = ss.str();
        content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());

        std::string dir = std::filesystem::path(path).parent_path().string();
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
                dir += '/';

        static const std::regex includeRe(R"re(^\s*#include\s+"([^"]+)"\s*$)re");
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line)) {
                if (line.rfind("#version", 0) == 0)
                        continue; // Only the top-level file may carry #version.
                std::smatch stageMatch;
                if (std::regex_match(line, stageMatch, kStagePragmaRe)) {
                        // Track the stage region so includes land in the right
                        // bucket; SplitStages applies the identical walk to the
                        // merged text. The pragma line itself is still emitted
                        // (SplitStages consumes it from the merged text).
                        stage = (stageMatch[1] == "vertex") ? 1 : 2;
                        out += line;
                        out += '\n';
                        continue;
                }
                std::smatch match;
                if (std::regex_match(line, match, includeRe)) {
                        const std::string incPath = match[1].str();
                        // Spec: relative to the including file. Fall back to the primary
                        // shader's directory for presets that assume a flat layout.
                        std::filesystem::path candidate = std::filesystem::path(dir) / incPath;
                        if (!std::filesystem::exists(candidate, ec))
                                candidate = std::filesystem::path(path).parent_path() / incPath;
                        // SEC-01: includes from untrusted .slang sources may only reach
                        // files inside the shaders root; absolute paths and ../ escapes
                        // are refused with the reason in the translation error.
                        if (!IsPathInsideShaderRoot(candidate)) {
                                error = StrCat("slang: include target outside the shaders root: ", incPath);
                                return false;
                        }
                        const std::string candidateCanonical = std::filesystem::weakly_canonical(candidate, ec).string();
                        // Include-once per stage bucket: drop repeats within the
                        // same region, keep deliberate re-includes across regions.
                        if (!visited[stage].insert(candidateCanonical.empty() ? candidate.string() : candidateCanonical).second)
                                continue;
                        out += "// begin include: " + incPath + "\n";
                        if (!ResolveIncludes(candidate.string(), depth + 1, stage, visited, active, out, error))
                                return false;
                        out += "// end include: " + incPath + "\n";
                } else {
                        out += line;
                        out += '\n';
                }
        }
        active.pop_back();
        return true;
}

struct StageSplit {
        std::string shared;
        std::string vertex;
        std::string fragment;
};

StageSplit SplitStages(const std::string &text)
{
        StageSplit split;
        const std::regex &stageRe = kStagePragmaRe;
        std::istringstream lines(text);
        std::string line;
        int stage = 0; // 0 = shared, 1 = vertex, 2 = fragment
        while (std::getline(lines, line)) {
                std::smatch match;
                if (std::regex_match(line, match, stageRe)) {
                        stage = (match[1] == "vertex") ? 1 : 2;
                        continue;
                }
                switch (stage) {
                case 0:
                        split.shared += line;
                        split.shared += '\n';
                        break;
                case 1:
                        split.vertex += line;
                        split.vertex += '\n';
                        break;
                case 2:
                        split.fragment += line;
                        split.fragment += '\n';
                        break;
                }
        }
        return split;
}

/** @brief Parses a `#pragma parameter` number RetroArch-style (ROB-01).
 *
 *  RetroArch parses pragma numbers with atof, which yields 0 for malformed
 *  tokens. std::stof instead throws on token shapes the pragma regex accepts
 *  ("." or "-" alone) and terminated translation through the uncaught
 *  exception. strtof with an end check reproduces the atof contract without
 *  throwing; a token with no consumed characters parses as 0. */
float ParsePragmaNumber(const std::string &value)
{
        char *end = nullptr;
        const float result = std::strtof(value.c_str(), &end);
        if (end == value.c_str())
                return 0.0F;
        return result;
}

/** @brief Extracts and removes `#pragma name/format/parameter` lines. */
void ExtractPragmas(std::string &text, std::vector<ParamInfo> &parameters, std::string &pragmaName,
    std::string &pragmaFormat)
{
        static const std::regex paramRe(
            // RetroArch's pragma parse is sscanf-style and ignores trailing
            // junk; 58 corpus pragma lines carry a trailing // (or /*) comment
            // and were silently dropped here, leaving their UBO members
            // unvalidated (field log: scanlines-Modern densY/densX/colAtten).
            R"re(^\s*#pragma\s+parameter\s+(\w+)\s+"([^"]*)"\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)(?:\s+([-+0-9.eE]+))?(?:\s*(?://|/\*).*)?\s*$)re");
        static const std::regex nameRe(R"(^\s*#pragma\s+name\s+(\w+)\s*$)");
        static const std::regex formatRe(R"(^\s*#pragma\s+format\s+(\w+)\s*$)");

        std::istringstream lines(text);
        std::string line;
        std::string kept;
        while (std::getline(lines, line)) {
                std::smatch match;
                if (std::regex_match(line, match, paramRe)) {
                        ParamInfo param;
                        param.name = match[1].str();
                        param.description = match[2].str();
                        param.defaultValue = ParsePragmaNumber(match[3].str());
                        param.minimum = ParsePragmaNumber(match[4].str());
                        param.maximum = ParsePragmaNumber(match[5].str());
                        if (match[6].matched)
                                param.step = ParsePragmaNumber(match[6].str());
                        parameters.push_back(std::move(param));
                        continue;
                }
                if (std::regex_match(line, match, nameRe)) {
                        pragmaName = match[1].str();
                        continue;
                }
                if (std::regex_match(line, match, formatRe)) {
                        pragmaFormat = match[1].str();
                        continue;
                }
                kept += line;
                kept += '\n';
        }
        text = std::move(kept);
}

struct BlockMatch {
        size_t matchStart;   // Offset of the layout prefix (or of "uniform" without one).
        size_t uniformBegin; // Offset of the "uniform" keyword.
        size_t bodyBegin;    // Offset just past '{'.
        size_t bodyEnd;      // Offset of the matching '}'.
        size_t declEnd;      // Offset just past the terminating ';' of the instance declaration.
        std::string blockName;
        std::string instanceName;
        std::string body;
};

/**
 * @brief Finds the next uniform block declaration that matches the requested kind.
 * @param pushConstant true to find the push-constant block, false for plain UBOs.
 */
bool FindUniformBlock(const std::string &text, size_t from, bool pushConstant, BlockMatch &out)
{
        static const std::regex headRe(R"((layout\s*\([^;{]*?\)\s*)?uniform\s+(\w+)\s*\{)");
        std::smatch match;
        auto searchFrom = text.cbegin() + static_cast<long>(from);
        while (std::regex_search(searchFrom, text.cend(), match, headRe)) {
                const bool isPush = match[1].matched && match[1].str().find("push_constant") != std::string::npos;
                if (isPush != pushConstant) {
                        searchFrom = match.suffix().first;
                        continue;
                }
                BlockMatch block;
                // match.position() is relative to the current search range start, which the
                // skip loop may have advanced past `from` (e.g. a push-constant block that
                // was skipped while looking for a plain UBO). Anchor to searchFrom instead.
                const size_t searchOffset = static_cast<size_t>(searchFrom - text.cbegin());
                block.matchStart = searchOffset + static_cast<size_t>(match.position(0));
                block.uniformBegin = block.matchStart + (match[1].matched ? match[1].length() : 0);
                block.bodyBegin = searchOffset + static_cast<size_t>(match.position(0)) + match.length();
                block.blockName = match[2].str();
                int depth = 1;
                size_t i = block.bodyBegin;
                for (; i < text.size() && depth > 0; ++i) {
                        if (text[i] == '{')
                                ++depth;
                        else if (text[i] == '}')
                                --depth;
                }
                if (depth != 0)
                        return false; // Unbalanced; glslang will report it.
                block.bodyEnd = i - 1;
                block.body = text.substr(block.bodyBegin, block.bodyEnd - block.bodyBegin);
                size_t j = block.bodyEnd + 1;
                while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j])) != 0)
                        ++j;
                std::string instance;
                while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) != 0 || text[j] == '_')) {
                        instance += text[j];
                        ++j;
                }
                while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j])) != 0)
                        ++j;
                if (j < text.size() && text[j] == ';')
                        ++j;
                block.instanceName = instance;
                block.declEnd = j;
                out = std::move(block);
                return true;
        }
        return false;
}

bool ParseUniformType(const std::string &typeToken, UniformType &type)
{
        static const std::unordered_map<std::string, UniformType> kTypes = {
                { "float", UniformType::Float },
                { "vec2", UniformType::Vec2 },
                { "vec3", UniformType::Vec3 },
                { "vec4", UniformType::Vec4 },
                { "int", UniformType::Int },
                { "ivec2", UniformType::IVec2 },
                { "ivec3", UniformType::IVec3 },
                { "ivec4", UniformType::IVec4 },
                { "uint", UniformType::Uint },
                { "uvec2", UniformType::UVec2 },
                { "uvec3", UniformType::UVec3 },
                { "uvec4", UniformType::UVec4 },
                { "bool", UniformType::Bool },
                { "mat4", UniformType::Mat4 },
                { "mat4x4", UniformType::Mat4 },
        };
        const auto it = kTypes.find(typeToken);
        if (it == kTypes.end())
                return false;
        type = it->second;
        return true;
}

void Std140TypeLayout(UniformType type, uint32_t &align, uint32_t &size)
{
        switch (type) {
        case UniformType::Float:
        case UniformType::Int:
        case UniformType::Uint:
        case UniformType::Bool:
                align = 4;
                size = 4;
                break;
        case UniformType::Vec2:
        case UniformType::IVec2:
        case UniformType::UVec2:
                align = 8;
                size = 8;
                break;
        case UniformType::Vec3:
        case UniformType::IVec3:
        case UniformType::UVec3:
                align = 16;
                size = 12;
                break;
        case UniformType::Vec4:
        case UniformType::IVec4:
        case UniformType::UVec4:
                align = 16;
                size = 16;
                break;
        case UniformType::Mat4:
                align = 16;
                size = 64;
                break;
        }
}

uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
        return (value + alignment - 1) / alignment * alignment;
}

uint32_t BlockNextOffset(const std::vector<UniformMember> &members)
{
        if (members.empty())
                return 0;
        const UniformMember &last = members.back();
        return last.offset + last.size;
}

bool ParseBlockMembers(const std::string &body, std::vector<UniformMember> &members, std::string &error)
{
        const std::string clean = StripComments(body);
        static const std::regex layoutRe(R"(layout\s*\([^)]*\))");
        size_t pos = 0;
        while (pos < clean.size()) {
                const size_t semi = clean.find(';', pos);
                if (semi == std::string::npos)
                        break;
                std::string decl = clean.substr(pos, semi - pos);
                pos = semi + 1;
                decl = std::regex_replace(decl, layoutRe, " ");
                for (const char *qualifier : { "flat", "smooth", "noperspective", "highp", "mediump", "lowp" }) {
                        const size_t q = decl.find(qualifier);
                        if (q != std::string::npos
                            && (q == 0 || std::isspace(static_cast<unsigned char>(decl[q - 1])) != 0)) {
                                decl.erase(q, std::strlen(qualifier));
                        }
                }
                std::istringstream words(decl);
                std::vector<std::string> tokens;
                std::string token;
                while (words >> token)
                        tokens.push_back(token);
                if (tokens.empty())
                        continue;
                if (tokens.size() < 2) {
                        error = StrCat("slang: unsupported uniform block member: '", decl, "'");
                        return false;
                }
                // "<type> <name>[N], <name2>" - the type is the first token naming a
                // supported std140 type; every token after it is a member name (this
                // also handles multi-name declarations like "float A, B;").
                size_t typeIdx = tokens.size();
                for (size_t t = 0; t + 1 < tokens.size(); ++t) {
                        UniformType probe;
                        if (ParseUniformType(tokens[t], probe)) {
                                typeIdx = t;
                                break;
                        }
                }
                if (typeIdx >= tokens.size() - 1) {
                        error = StrCat("slang: unsupported uniform block member: '", decl, "'");
                        return false;
                }
                UniformType type;
                (void)ParseUniformType(tokens[typeIdx], type);
                uint32_t align = 0;
                uint32_t size = 0;
                Std140TypeLayout(type, align, size);
                // Explode comma-separated declarators into a flat name list. The
                // whitespace tokenizer hands a no-space list like
                // "float sc_sat,sc_black,..." over as ONE token and erasing the
                // commas inside it GLUED the names into a single bogus member
                // ("sc_satsc_blacksc_..." - field log: simple_color_controls,
                // anime4k, ossc). Splitting on ',' recovers the real names.
                std::vector<std::string> nameTokens;
                for (size_t n = typeIdx + 1; n < tokens.size(); ++n) {
                        const std::string &list = tokens[n];
                        size_t start = 0;
                        while (start <= list.size()) {
                                const size_t comma = list.find(',', start);
                                const size_t end = comma == std::string::npos ? list.size() : comma;
                                if (end > start)
                                        nameTokens.push_back(list.substr(start, end - start));
                                start = end + 1;
                        }
                }
                for (const std::string &nameToken : nameTokens) {
                        if (nameToken.empty())
                                continue;
                        UniformMember member;
                        member.type = type;
                        const size_t bracket = nameToken.find('[');
                        if (bracket != std::string::npos) {
                                const size_t close = nameToken.find(']', bracket);
                                const std::string countText = bracket + 1 <= close ? nameToken.substr(bracket + 1, close - bracket - 1) : "";
                                member.name = nameToken.substr(0, bracket);
                                if (countText.empty()) {
                                        error = StrCat("slang: unsized arrays are not supported in uniform blocks: '", member.name, "'");
                                        return false;
                                }
                                try {
                                        member.arraySize = std::stoi(countText);
                                } catch (const std::exception &) {
                                        error = StrCat("slang: invalid array size for member: '", member.name, "'");
                                        return false;
                                }
                                if (member.arraySize <= 0 || member.arraySize > 64) {
                                        error = StrCat("slang: invalid array size for member: '", member.name, "'");
                                        return false;
                                }
                        } else {
                                member.name = nameToken;
                        }
                        if (member.arraySize > 0) {
                                // std140: array stride is rounded up to a full vec4 (16 bytes).
                                const uint32_t stride = AlignUp(size, std::max<uint32_t>(align, 16));
                                member.size = stride * static_cast<uint32_t>(member.arraySize);
                                member.offset = AlignUp(BlockNextOffset(members), std::max<uint32_t>(align, 16));
                        } else {
                                member.size = size;
                                member.offset = AlignUp(BlockNextOffset(members), align);
                        }
                        members.push_back(std::move(member));
                }
        }
        return true;
}

/** @brief Rewrites plain UBO declarations to the libslang-vk descriptor set conventions. */
bool RewriteUboBlocks(std::string &glsl, bool isVertex, std::vector<UniformBlock> &blocks, std::string &error)
{
        const uint32_t set = isVertex ? 1 : 3;
        uint32_t binding = 0;
        size_t from = 0;
        for (;;) {
                BlockMatch block;
                if (!FindUniformBlock(glsl, from, /*pushConstant=*/false, block))
                        break;
                if (blocks.size() >= 1) {
                        error = "slang: more than one uniform block is not supported (spec allows one UBO)";
                        return false;
                }
                if (binding >= 2) {
                        error = "slang: too many uniform blocks";
                        return false;
                }
                UniformBlock ubo;
                ubo.binding = binding;
                ubo.instanceName = block.instanceName;
                if (!ParseBlockMembers(block.body, ubo.members, error))
                        return false;
                const std::string replacement = StrCat("layout(std140, set = ", set, ", binding = ", binding, ")");
                glsl = glsl.substr(0, block.matchStart) + replacement + glsl.substr(block.uniformBegin);
                blocks.push_back(std::move(ubo));
                // Continue searching after the rewritten declaration. The replacement has
                // consumed the original layout qualifier, so the next position is
                // matchStart + |replacement| + (declEnd - uniformBegin).
                from = block.matchStart + replacement.size() + (block.declEnd - block.uniformBegin);
                ++binding;
        }
        return true;
}

/** @brief Re-homes the push-constant block as a std140 UBO in the libslang-vk conventions. */
bool RewritePushBlock(std::string &glsl, bool isVertex, bool uboPresent, std::vector<UniformBlock> &blocks,
    std::string &error)
{
        const uint32_t set = isVertex ? 1 : 3;
        const uint32_t binding = uboPresent ? 1 : 0;
        BlockMatch block;
        if (!FindUniformBlock(glsl, 0, /*pushConstant=*/true, block))
                return true; // No push block: fine.
        UniformBlock pushBlock;
        pushBlock.binding = binding;
        pushBlock.instanceName = block.instanceName;
        if (!ParseBlockMembers(block.body, pushBlock.members, error))
                return false;
        const std::string replacement = StrCat("layout(std140, set = ", set, ", binding = ", binding, ")");
        glsl = glsl.substr(0, block.matchStart) + replacement + glsl.substr(block.uniformBegin);
        blocks.push_back(std::move(pushBlock));
        return true;
}

bool IsBuiltinTextureName(const std::string &name)
{
        static const std::regex builtinRe(R"(^(Source|Original|(OriginalHistory|PassOutput|PassFeedback|User)(0|[1-9][0-9]*))$)");
        return std::regex_match(name, builtinRe);
}

int BuiltinTextureIndex(const std::string &name)
{
        static const std::regex indexedRe(R"(^(OriginalHistory|PassOutput|PassFeedback|User)(0|[1-9][0-9]*)$)");
        std::smatch match;
        if (!std::regex_match(name, match, indexedRe))
                return -1;
        // ROB-01: the regex accepts arbitrarily long digit strings, so a
        // sampler named OriginalHistory99999999999999 matched but threw
        // std::out_of_range out of std::stoi. Non-throwing parse; a failing
        // index maps to -1, which every caller treats as "not a valid index".
        const ParseIntResult<int> idx = ParseInt<int>(match[2].str());
        return idx.has_value() ? *idx : -1;
}

/** @brief True when the source #defines this identifier. Field round 2: Mega
 * Bezel wrapper shaders declare samplers that are macro-renamed to known
 * textures; glslang expands the sampling calls, so only the declaration
 * needs dropping. Runs on comment-stripped text. */
bool IsDefineTarget(const std::string &glsl, const std::string &name)
{
        size_t pos = 0;
        while ((pos = glsl.find("#define", pos)) != std::string::npos) {
                // The directive must start a line (leading whitespace allowed).
                const size_t lineStart = glsl.rfind('\n', pos) == std::string::npos ? 0 : glsl.rfind('\n', pos) + 1;
                bool onlySpace = true;
                for (size_t i = lineStart; i < pos; ++i) {
                        if (glsl[i] != ' ' && glsl[i] != '\t') {
                                onlySpace = false;
                                break;
                        }
                }
                const size_t after = pos + 7;
                size_t nameStart = after;
                while (nameStart < glsl.size() && (glsl[nameStart] == ' ' || glsl[nameStart] == '\t'))
                        ++nameStart;
                if (onlySpace && nameStart + name.size() <= glsl.size()
                    && glsl.compare(nameStart, name.size(), name) == 0) {
                        const char end = nameStart + name.size() < glsl.size() ? glsl[nameStart + name.size()] : '\0';
                        if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_')
                                return true;
                }
                pos = after;
        }
        return false;
}

/** @brief True when the identifier appears as a sampling-call argument
 * (texture/textureLod/textureOffset/... or texelFetch/texelFetchOffset).
 * The declaration itself ("sampler2D name;") does not count: the word in
 * front of the name there is "sampler2D", not a call. */
bool IsTextureSampled(const std::string &glsl, const std::string &name)
{
        size_t pos = 0;
        while ((pos = glsl.find(name, pos)) != std::string::npos) {
                const size_t end = pos + name.size();
                const char after = end < glsl.size() ? glsl[end] : '\0';
                const bool wordEnd = !std::isalnum(static_cast<unsigned char>(after)) && after != '_';
                if (wordEnd) {
                        size_t begin = pos;
                        while (begin > 0
                            && (std::isalnum(static_cast<unsigned char>(glsl[begin - 1])) || glsl[begin - 1] == '_'))
                                --begin;
                        if (begin < pos) {
                                // Identifier directly attached (declaration "sampler2D Name",
                                // member access "params.Name"): never a sampling call.
                                const std::string prefix = glsl.substr(begin, pos - begin);
                                if (prefix.compare(0, 7, "texture") == 0 || prefix.compare(0, 10, "texelFetch") == 0)
                                        return true;
                        } else {
                                // A sampling call separates callee and argument with '(' and
                                // optional whitespace: "texture(Name" / "texture (Name".
                                size_t probe = pos;
                                while (probe > 0 && (glsl[probe - 1] == ' ' || glsl[probe - 1] == '\t'))
                                        --probe;
                                if (probe > 0 && glsl[probe - 1] == '(') {
                                        const size_t paren = probe - 1;
                                        size_t wordBegin = paren;
                                        while (wordBegin > 0
                                            && (std::isalnum(static_cast<unsigned char>(glsl[wordBegin - 1]))
                                                || glsl[wordBegin - 1] == '_'))
                                                --wordBegin;
                                        const std::string call = glsl.substr(wordBegin, paren - wordBegin);
                                        if (call.compare(0, 7, "texture") == 0 || call.compare(0, 10, "texelFetch") == 0)
                                                return true;
                                }
                        }
                }
                pos = end;
        }
        return false;
}

/** @brief Returns the single-identifier body of "#define <name> <body>" when
 * the source defines name as an object-like macro aliasing exactly one
 * identifier, or an empty string otherwise (no such define, function-like
 * macro, empty or multi-token body). Field round 3 companion to
 * IsDefineTarget. Runs on comment-stripped text. */
std::string DefineTargetExpansion(const std::string &glsl, const std::string &name)
{
        size_t pos = 0;
        while ((pos = glsl.find("#define", pos)) != std::string::npos) {
                // The directive must start a line (leading whitespace allowed).
                const size_t lineStart = glsl.rfind('\n', pos) == std::string::npos ? 0 : glsl.rfind('\n', pos) + 1;
                bool onlySpace = true;
                for (size_t i = lineStart; i < pos; ++i) {
                        if (glsl[i] != ' ' && glsl[i] != '\t') {
                                onlySpace = false;
                                break;
                        }
                }
                const size_t after = pos + 7;
                size_t nameStart = after;
                while (nameStart < glsl.size() && (glsl[nameStart] == ' ' || glsl[nameStart] == '\t'))
                        ++nameStart;
                if (onlySpace && nameStart + name.size() <= glsl.size()
                    && glsl.compare(nameStart, name.size(), name) == 0) {
                        const size_t nameEnd = nameStart + name.size();
                        const char end = nameEnd < glsl.size() ? glsl[nameEnd] : '\0';
                        // Object-like only: a "(" opens a function-like parameter list.
                        if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_' && end != '(') {
                                size_t lineEnd = glsl.find('\n', nameEnd);
                                if (lineEnd == std::string::npos)
                                        lineEnd = glsl.size();
                                const std::string body = TrimAll(glsl.substr(nameEnd, lineEnd - nameEnd));
                                bool singleIdent = !body.empty();
                                for (const char c : body) {
                                        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                                                singleIdent = false;
                                                break;
                                        }
                                }
                                if (singleIdent)
                                        return body;
                        }
                }
                pos = after;
        }
        return "";
}

/** @brief True when "sampler2D <name>" is declared in the (comment-stripped)
 * source. Field round 3: decides whether a macro-aliased declaration must be
 * kept — if the expansion target is already declared, the aliased
 * declaration is a redundant orphan (Mega Bezel "#define PrePass0 Source");
 * if not, the aliased declaration is the only one the macro-expanded
 * sampling calls can bind (nnedi3 deinterlacing combine passes). */
bool IsSamplerDeclared(const std::string &glsl, const std::string &name)
{
        size_t pos = 0;
        while ((pos = glsl.find("sampler2D", pos)) != std::string::npos) {
                // Skip isampler2D/usampler2D (identifier character right before).
                if (pos > 0
                    && (std::isalnum(static_cast<unsigned char>(glsl[pos - 1])) || glsl[pos - 1] == '_')) {
                        pos += 9;
                        continue;
                }
                size_t after = pos + 9;
                // A plain sampler2D declaration requires whitespace between the
                // keyword and the name; sampler2DArray/sampler2DShadow etc. skip.
                if (after >= glsl.size() || (glsl[after] != ' ' && glsl[after] != '\t')) {
                        pos += 9;
                        continue;
                }
                while (after < glsl.size() && (glsl[after] == ' ' || glsl[after] == '\t'))
                        ++after;

                if (after + name.size() <= glsl.size()
                    && glsl.compare(after, name.size(), name) == 0) {
                        const char end = after + name.size() < glsl.size() ? glsl[after + name.size()] : '\0';
                        if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_')
                                return true;
                }
                pos += 9;
        }
        return false;
}

/** @brief True when name appears anywhere besides its own sampler2D
 * declaration(s): sampling calls, user-function arguments, anything. Field
 * round 3: decides whether a macro-aliased declaration must be KEPT — the
 * post-expansion references need it (koko-aio passes FPS_ESTIMATE_PASS into
 * get_fps(...), which the texture-call sampling heuristic misses). */
bool IsSamplerNameUsed(const std::string &glsl, const std::string &name)
{
        size_t pos = 0;
        bool used = false;
        while ((pos = glsl.find(name, pos)) != std::string::npos) {
                const size_t end = pos + name.size();
                const char after = end < glsl.size() ? glsl[end] : '\0';
                const char before = pos > 0 ? glsl[pos - 1] : '\0';
                const bool wordEnd = !std::isalnum(static_cast<unsigned char>(after)) && after != '_';
                const bool wordStart = !std::isalnum(static_cast<unsigned char>(before)) && before != '_';
                if (wordEnd && wordStart) {
                        // Classify: sampler2D declaration or #define directive (the
                        // defined name itself), or a use.
                        size_t probe = pos;
                        while (probe > 0 && (glsl[probe - 1] == ' ' || glsl[probe - 1] == '\t'))
                                --probe;
                        bool directive = false;
                        for (const char *kw : { "sampler2D", "#define" }) {
                                const size_t len = std::strlen(kw);
                                if (probe >= len && glsl.compare(probe - len, len, kw) == 0
                                    && (probe - len == 0
                                        || (!std::isalnum(static_cast<unsigned char>(glsl[probe - len - 1]))
                                            && glsl[probe - len - 1] != '_'))) {
                                        directive = true;
                                        break;
                                }
                        }
                        if (!directive) {
                                used = true;
                                break;
                        }
                }
                pos = end;
        }
        return used;
}

/** @brief True when the name is a plain GLSL identifier - the only shape the
 * one-pass sampler scan index (PERF-06) is keyed for. */
bool IsIdentifier(const std::string &name)
{
        if (name.empty())
                return false;
        for (const char c : name) {
                if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_')
                        return false;
        }
        return true;
}

/** @brief One-pass index over the (comment-stripped) stage text answering the
 * per-declaration sampler resolution queries — PERF-06. RewriteSamplers used
 * to walk the entire stage text once per unknown sampler declaration
 * (IsDefineTarget, IsTextureSampled, then IsSamplerNameUsed /
 * DefineTargetExpansion / IsSamplerDeclared in the #defined-away path), so a
 * pack with N orphan declarations cost O(N·n) full-text scans on the frame
 * path during preset load. The index is built in two linear passes before the
 * rewrite loop (the text is not mutated inside the loop) and answers the same
 * five queries by lookup.
 *
 * Semantics parity with the five scans, per query:
 * - defineTargets / defineExpansion / samplerDecls record every "#define"
 *   directive target (with the object-like single-identifier body when one
 *   exists) and every "sampler2D <name>" declaration, walking the keyword
 *   occurrences with the exact branch logic of IsDefineTarget /
 *   DefineTargetExpansion / IsSamplerDeclared.
 * - usedNames records every identifier token that IsSamplerNameUsed would
 *   classify as a use (word-start/end occurrence not preceded — over spaces
 *   and tabs — by the "sampler2D" or "#define" directive keywords).
 * - sampledNames records every identifier token IsTextureSampled would accept
 *   in its standalone branch: preceded, over spaces and tabs, by '(' with a
 *   texture or texelFetch callee word in front of the paren.
 * - textureSuffixNames reproduces IsTextureSampled's attached-prefix branch
 *   for raw mid-token occurrences: a query name that is a suffix of a token
 *   whose prefix starts with "texture" (offset >= 7) or "texelFetch"
 *   (offset >= 10) and which extends to the token end classifies as sampled
 *   ("textureName" containing "Name").
 * The index only answers identifier-shaped names (everything the sampler
 * declaration grammar yields in practice); callers keep the original scans
 * for anything else. */
struct SamplerScanIndex {
        std::unordered_set<std::string> defineTargets;
        std::unordered_map<std::string, std::string> defineExpansion;
        std::unordered_set<std::string> samplerDecls;
        std::unordered_set<std::string> sampledNames;
        std::unordered_set<std::string> textureSuffixNames;
        std::unordered_set<std::string> usedNames;
};

SamplerScanIndex BuildSamplerScanIndex(const std::string &glsl)
{
        SamplerScanIndex idx;
        // Pass 1: directive keywords — the walk and branch logic mirror
        // IsDefineTarget / DefineTargetExpansion / IsSamplerDeclared exactly.
        {
                size_t pos = 0;
                while ((pos = glsl.find("#define", pos)) != std::string::npos) {
                        // The directive must start a line (leading whitespace allowed).
                        const size_t lineStart = glsl.rfind('\n', pos) == std::string::npos ? 0 : glsl.rfind('\n', pos) + 1;
                        bool onlySpace = true;
                        for (size_t i = lineStart; i < pos; ++i) {
                                if (glsl[i] != ' ' && glsl[i] != '\t') {
                                        onlySpace = false;
                                        break;
                                }
                        }
                        const size_t after = pos + 7;
                        size_t nameStart = after;
                        while (nameStart < glsl.size() && (glsl[nameStart] == ' ' || glsl[nameStart] == '\t'))
                                ++nameStart;
                        size_t nameEnd = nameStart;
                        while (nameEnd < glsl.size()
                            && (std::isalnum(static_cast<unsigned char>(glsl[nameEnd])) != 0 || glsl[nameEnd] == '_'))
                                ++nameEnd;
                        if (onlySpace && nameEnd > nameStart) {
                                const char end = nameEnd < glsl.size() ? glsl[nameEnd] : '\0';
                                if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_')
                                        idx.defineTargets.insert(glsl.substr(nameStart, nameEnd - nameStart));
                        }
                        // Object-like macro body, for the alias-resolution path.
                        if (onlySpace && nameEnd > nameStart) {
                                const char end = nameEnd < glsl.size() ? glsl[nameEnd] : '\0';
                                if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_' && end != '(') {
                                        size_t lineEnd = glsl.find('\n', nameEnd);
                                        if (lineEnd == std::string::npos)
                                                lineEnd = glsl.size();
                                        const std::string body = TrimAll(glsl.substr(nameEnd, lineEnd - nameEnd));
                                        bool singleIdent = !body.empty();
                                        for (const char c : body) {
                                                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                                                        singleIdent = false;
                                                        break;
                                                }
                                        }
                                        if (singleIdent)
                                                idx.defineExpansion.emplace(glsl.substr(nameStart, nameEnd - nameStart), body);
                                }
                        }
                        pos = after;
                }
        }
        {
                size_t pos = 0;
                while ((pos = glsl.find("sampler2D", pos)) != std::string::npos) {
                        // Skip isampler2D/usampler2D (identifier character right before).
                        if (pos > 0
                            && (std::isalnum(static_cast<unsigned char>(glsl[pos - 1])) != 0 || glsl[pos - 1] == '_')) {
                                pos += 9;
                                continue;
                        }
                        size_t after = pos + 9;
                        // A plain sampler2D declaration requires whitespace between the
                        // keyword and the name; sampler2DArray/sampler2DShadow etc. skip.
                        if (after >= glsl.size() || (glsl[after] != ' ' && glsl[after] != '\t')) {
                                pos += 9;
                                continue;
                        }
                        while (after < glsl.size() && (glsl[after] == ' ' || glsl[after] == '\t'))
                                ++after;
                        size_t nameEnd = after;
                        while (nameEnd < glsl.size()
                            && (std::isalnum(static_cast<unsigned char>(glsl[nameEnd])) != 0 || glsl[nameEnd] == '_'))
                                ++nameEnd;
                        if (nameEnd > after) {
                                const char end = nameEnd < glsl.size() ? glsl[nameEnd] : '\0';
                                if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_')
                                        idx.samplerDecls.insert(glsl.substr(after, nameEnd - after));
                        }
                        pos += 9;
                }
        }
        // Pass 2: identifier tokens — one walk classifies every occurrence the
        // per-name scans would have found.
        const size_t n = glsl.size();
        auto isWord = [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        };
        size_t i = 0;
        while (i < n) {
                if (!isWord(glsl[i])) {
                        ++i;
                        continue;
                }
                const size_t ts = i;
                while (i < n && isWord(glsl[i]))
                        ++i;
                const std::string token = glsl.substr(ts, i - ts);
                // Walk back over spaces and tabs — shared by the IsSamplerNameUsed
                // directive check and IsTextureSampled's call-argument check.
                size_t probe = ts;
                while (probe > 0 && (glsl[probe - 1] == ' ' || glsl[probe - 1] == '\t'))
                        --probe;
                bool directive = false;
                for (const char *kw : { "sampler2D", "#define" }) {
                        const size_t len = std::strlen(kw);
                        if (probe >= len && glsl.compare(probe - len, len, kw) == 0
                            && (probe - len == 0
                                || (!std::isalnum(static_cast<unsigned char>(glsl[probe - len - 1]))
                                    && glsl[probe - len - 1] != '_'))) {
                                directive = true;
                                break;
                        }
                }
                if (!directive)
                        idx.usedNames.insert(token);
                if (probe > 0 && glsl[probe - 1] == '(') {
                        const size_t paren = probe - 1;
                        size_t wordBegin = paren;
                        while (wordBegin > 0 && isWord(glsl[wordBegin - 1]))
                                --wordBegin;
                        const std::string call = glsl.substr(wordBegin, paren - wordBegin);
                        if (call.compare(0, 7, "texture") == 0 || call.compare(0, 10, "texelFetch") == 0)
                                idx.sampledNames.insert(token);
                }
                // Attached-prefix parity: suffixes of texture*/texelFetch*-prefixed
                // tokens (the raw scan's mid-token occurrences that end the token).
                if (token.size() > 7 && token.compare(0, 7, "texture") == 0) {
                        for (size_t k = 7; k < token.size(); ++k)
                                idx.textureSuffixNames.insert(token.substr(k));
                } else if (token.size() > 10 && token.compare(0, 10, "texelFetch") == 0) {
                        for (size_t k = 10; k < token.size(); ++k)
                                idx.textureSuffixNames.insert(token.substr(k));
                }
        }
        return idx;
}

/** @brief Indexed fast paths for the per-declaration resolution queries
 * (PERF-06): identifier-shaped names - everything the sampler declaration
 * grammar yields in practice - answer from the one-pass index; anything else
 * (invalid GLSL that glslang rejects later) keeps the original full-text scan
 * for exact parity. */
bool IsDefineTarget(const SamplerScanIndex &idx, const std::string &glsl, const std::string &name)
{
        if (IsIdentifier(name))
                return idx.defineTargets.count(name) != 0;
        return IsDefineTarget(glsl, name);
}

bool IsTextureSampled(const SamplerScanIndex &idx, const std::string &glsl, const std::string &name)
{
        if (IsIdentifier(name))
                return idx.sampledNames.count(name) != 0 || idx.textureSuffixNames.count(name) != 0;
        return IsTextureSampled(glsl, name);
}

bool IsSamplerNameUsed(const SamplerScanIndex &idx, const std::string &glsl, const std::string &name)
{
        if (IsIdentifier(name))
                return idx.usedNames.count(name) != 0;
        return IsSamplerNameUsed(glsl, name);
}

std::string DefineTargetExpansion(const SamplerScanIndex &idx, const std::string &glsl, const std::string &name)
{
        if (IsIdentifier(name)) {
                const auto it = idx.defineExpansion.find(name);
                return it != idx.defineExpansion.end() ? it->second : std::string();
        }
        return DefineTargetExpansion(glsl, name);
}

bool IsSamplerDeclared(const SamplerScanIndex &idx, const std::string &glsl, const std::string &name)
{
        if (IsIdentifier(name))
                return idx.samplerDecls.count(name) != 0;
        return IsSamplerDeclared(glsl, name);
}

/** @brief Rewrites sampler2D declarations to the fragment texture set (set 2) with dense bindings. */
bool RewriteSamplers(std::string &glsl, bool isVertex, StageTranslation &stage,
    const std::unordered_set<std::string> &knownTextureNames, int passIndex, int passCount,
    std::vector<std::string> &warnings, std::string &error)
{
        static const std::regex samplerRe(
            R"(^(\s*)(layout\s*\([^)]*\)\s*)?uniform\s+((?:lowp\s+|mediump\s+|highp\s+)?)sampler2D\s+(.+?)\s*;\s*$)");
        std::istringstream lines(glsl);
        std::string line;
        std::string out;
        out.reserve(glsl.size() + 64);
        // PERF-06: one linear pass over the stage text replaces the five full-text
        // rescans the per-declaration queries below used to make (O(N*n) on wrapper
        // packs with many orphan declarations). The text is not mutated inside the
        // rewrite loop, so the index stays valid.
        const SamplerScanIndex scanIndex = BuildSamplerScanIndex(glsl);
        const uint32_t texSet = isVertex ? 0U : 2U;
        uint32_t binding = 0;
        while (std::getline(lines, line)) {
                std::smatch match;
                if (!std::regex_match(line, match, samplerRe)) {
                        out += line;
                        out += '\n';
                        continue;
                }
                const std::string precision = match[3].str();
                std::string namesText = match[4].str();
                // Split on commas: "A, B" (arrays are not supported).
                std::vector<std::string> names;
                std::istringstream nameStream(namesText);
                std::string nameToken;
                while (std::getline(nameStream, nameToken, ',')) {
                        names.push_back(TrimAll(nameToken));
                }
                for (const std::string &declaredName : names) {
                        if (declaredName.empty()) {
                                error = "slang: empty sampler name";
                                return false;
                        }
                        if (declaredName.find('[') != std::string::npos) {
                                error = StrCat("slang: sampler arrays are not supported: '", declaredName, "'");
                                return false;
                        }
                        // Field round 3: the declared name may be an object-like macro alias;
                        // the effective texture name is the macro expansion.
                        std::string name = declaredName;
                        if (!IsBuiltinTextureName(name)) {
                                std::string base = name;
                                if (base.size() > 8 && base.compare(base.size() - 8, 8, "Feedback") == 0)
                                        base = base.substr(0, base.size() - 8);
                                // Case-insensitive match against the preset's declared names:
                                // GLSL is case-sensitive and presets/shaders disagree on case.
                                std::string baseLower;
                                baseLower.reserve(base.size());
                                for (const char c : base)
                                        baseLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                if (knownTextureNames.find(baseLower) == knownTextureNames.end()) {
                                        // Field round 2 tolerance: wrapper shaders ship orphan sampler
                                        // declarations for chains they are not part of. Mega Bezel's
                                        // hsm-custom-fast-sharpen declares PrePass0 and #defines it to
                                        // Source (glslang expands the calls); uborder's single-pass
                                        // crt-geom declares ambi_temporal_pass without ever sampling
                                        // it. RetroArch binds nothing for either, so drop the
                                        // declaration with a warning when the name is a #define target
                                        // or is never sampled. A sampled unknown name still hard-fails:
                                        // that is a real missing dependency.
                                        const bool definedAway = IsDefineTarget(scanIndex, glsl, name);
                                        if (!definedAway && IsTextureSampled(scanIndex, glsl, name)) {
                                                error = StrCat("slang: unknown texture reference '", name,
                                                    "' (not a builtin, alias or lookup texture)");
                                                return false;
                                        }
                                        // Field round 3: an object-like macro can alias the DECLARATION
                                        // itself. nnedi3-combine-v-pass0.slang does "#define
                                        // NNEDI3_PREV_TEXTURE nnediPass0" and its include declares
                                        // "uniform sampler2D NNEDI3_PREV_TEXTURE;" — glslang's preprocessor
                                        // renames that declaration AND every sampling call to nnediPass0,
                                        // so dropping the declaration left the calls referencing an
                                        // undeclared identifier (field log: nnedi3-nns*-deinterlacing
                                        // pass 3). When the alias is referenced beyond its own
                                        // declaration in this stage and the #define body is a single
                                        // identifier that is not independently declared here, keep the
                                        // declaration under that identifier (the preset binds it via
                                        // its alias).
                                        // When the expansion target IS declared (Mega Bezel's "#define
                                        // PrePass0 Source"), dropping stays correct: the calls expand to
                                        // the existing declaration.
                                        bool resolved = false;
                                        if (definedAway && IsSamplerNameUsed(scanIndex, glsl, name)) {
                                                const std::string expansion = DefineTargetExpansion(scanIndex, glsl, name);
                                                if (!expansion.empty() && !IsSamplerDeclared(scanIndex, glsl, expansion)) {
                                                        bool alreadyEmitted = false;
                                                        for (const TextureRef &known : stage.textures) {
                                                                if (known.name == expansion) {
                                                                        alreadyEmitted = true;
                                                                        break;
                                                                }
                                                        }
                                                        if (!alreadyEmitted) {
                                                                name = expansion;
                                                                resolved = IsBuiltinTextureName(name);
                                                                if (!resolved) {
                                                                        std::string expandedBase = name;
                                                                        if (expandedBase.size() > 8 && expandedBase.compare(expandedBase.size() - 8, 8, "Feedback") == 0)
                                                                                expandedBase = expandedBase.substr(0, expandedBase.size() - 8);
                                                                        std::string expandedLower;
                                                                        expandedLower.reserve(expandedBase.size());
                                                                        for (const char c : expandedBase)
                                                                                expandedLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                                                        resolved = knownTextureNames.find(expandedLower) != knownTextureNames.end();
                                                                }
                                                        }
                                                }
                                        }
                                        if (!resolved) {
                                                warnings.push_back(StrCat("sampler '", declaredName, "' has no chain target (",
                                                    definedAway ? "#defined away" : "declared but never sampled",
                                                    "); dropping declaration"));
                                                continue;
                                        }
                                }
                        }
                        // PassOutput# causality: pass N may only read outputs of passes < N.
                        if (name.rfind("PassOutput", 0) == 0) {
                                const int idx = BuiltinTextureIndex(name);
                                if (idx >= passIndex || idx >= passCount) {
                                        error = StrCat("slang: PassOutput", idx, " is not causal in pass ", passIndex);
                                        return false;
                                }
                        }
                        if (name.rfind("PassFeedback", 0) == 0) {
                                const int idx = BuiltinTextureIndex(name);
                                if (idx >= passCount || idx < 0) {
                                        error = StrCat("slang: PassFeedback", idx, " does not exist (", passCount, " passes)");
                                        return false;
                                }
                        }
                        if (name.rfind("OriginalHistory", 0) == 0) {
                                const int idx = BuiltinTextureIndex(name);
                                if (idx < 0 || idx > 8) {
                                        error = StrCat("slang: OriginalHistory", idx, " is out of range (0-8)");
                                        return false;
                                }
                        }
                        // ROB-21: include-once is per stage bucket, so a stage-guarded
                        // header included once in the shared region and again in the
                        // fragment region pastes its sampler declaration into this
                        // translation unit twice (split.shared + split.fragment) - valid
                        // input that used to emit two dense bindings for one sampler
                        // (glslang "redefinition" failure with misleading diagnostics, and
                        // stage.textures double-counting into num_samplers). Name-dedupe
                        // the plain rewrite path: first binding wins, the repeat is
                        // dropped (the earlier declaration is visible everywhere below it
                        // in the same TU).
                        bool alreadyDeclared = false;
                        for (const TextureRef &known : stage.textures) {
                                if (known.name == name) {
                                        alreadyDeclared = true;
                                        break;
                                }
                        }
                        if (alreadyDeclared) {
                                warnings.push_back(StrCat("sampler '", name,
                                    "' is declared more than once in the stage (a stage-guarded header was re-included); keeping the first binding"));
                                continue;
                        }
                        out += StrCat("layout(set = ", texSet, ", binding = ", binding, ") uniform ", precision, "sampler2D ", name, ";\n");
                        stage.textures.push_back({ name, binding });
                        ++binding;
                }
        }
        glsl = std::move(out);
        if (isVertex && !stage.textures.empty()) {
                error = "slang: vertex-stage texture sampling is not supported yet";
                return false;
        }
        return true;
}

/** @brief Checks block members: recognized semantics/sizes/parameters only get values.
 * Unknown members are tolerated RetroArch-style: collected as warnings and left at
 * zero (field round 1: auto_res, XBR_ANTI_RINGING, ia_FLIP_HORZ all hard-failed
 * translation here for shaders RetroArch loads fine). */
bool ValidateBlockMembers(const std::vector<UniformBlock> &blocks,
    const std::vector<ParamInfo> &parameters, const std::unordered_set<std::string> &knownTextureNames,
    std::vector<std::string> &warnings)
{
        for (const UniformBlock &block : blocks) {
                for (const UniformMember &member : block.members) {
                        const std::string &name = member.name;
                        bool ok = (name == "MVP" || name == "OutputSize" || name == "FinalViewportSize" || name == "OriginalSize"
                            || name == "SourceSize" || name == "FrameCount" || name == "FrameDirection" || name == "FrameTime"
                            || name == "PASSINDEX" || name == "Rotation");
                        if (!ok) {
                                std::string base = name;
                                bool sizeForm = false;
                                bool feedbackSizeForm = false;
                                if (base.size() > 4 && base.compare(base.size() - 4, 4, "Size") == 0) {
                                        base = base.substr(0, base.size() - 4);
                                        sizeForm = true;
                                }
                                if (base.size() > 8 && base.compare(base.size() - 8, 8, "Feedback") == 0) {
                                        base = base.substr(0, base.size() - 8);
                                        feedbackSizeForm = true;
                                }
                                if ((sizeForm || feedbackSizeForm)
                                    && (base == "Original" || base == "Source" || knownTextureNames.find(base) != knownTextureNames.end()
                                        || IsBuiltinTextureName(base))) {
                                        ok = true;
                                }
                        }
                        if (!ok) {
                                for (const ParamInfo &param : parameters) {
                                        if (param.name.size() == name.size()
                                            && std::equal(param.name.begin(), param.name.end(), name.begin(),
                                                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
                                                ok = true;
                                                break;
                                        }
                                }
                        }
                        if (!ok) {
                                // RetroArch leaves unrecognized UBO members at 0. Downgrade
                                // the loud translate failure to a logged warning; the
                                // std140 layout is unaffected and FillBlockBytes already
                                // writes the zero-fill fallback for unmatched members.
                                // * This bloats the log very fast, over 1MB - VTSTech *
                                // warnings.push_back(StrCat("uniform block member '", name,
                                //  "' is not a builtin semantic, texture size, feedback size or #pragma parameter; filling 0"));
                        }
                }
        }
        return true;
}

/** @brief Ensures the vertex stage declares the two required inputs and a #version. */
void EnsureVertexInterface(std::string &glsl)
{
        const bool hasVersion = glsl.rfind("#version", 0) == 0;
        static const std::regex posInRe(R"(layout\s*\(\s*location\s*=\s*0\s*\)\s*in\s+vec4\s+\w+)");
        static const std::regex texInRe(R"(layout\s*\(\s*location\s*=\s*1\s*\)\s*in\s+vec2\s+\w+)");
        const bool hasPosition = std::regex_search(glsl, posInRe);
        const bool hasTexCoord = std::regex_search(glsl, texInRe);
        std::string prologue;
        if (!hasVersion)
                prologue += "#version 450\n";
        if (!hasPosition)
                prologue += "layout(location = 0) in vec4 Position;\n";
        if (!hasTexCoord)
                prologue += "layout(location = 1) in vec2 TexCoord;\n";
        if (!prologue.empty()) {
                if (hasVersion) {
                        // Insert after the #version line.
                        const size_t eol = glsl.find('\n');
                        glsl = glsl.substr(0, eol + 1) + prologue + glsl.substr(eol + 1);
                } else {
                        glsl = prologue + glsl;
                }
        }
}

/** @brief Injects the stock vertex stage when a shader only provides a fragment stage. */
std::string StockVertexSource()
{
        return R"(#version 450
layout(std140, set = 1, binding = 0) uniform UBO
{
    mat4 MVP;
    vec4 OutputSize;
    vec4 OriginalSize;
    vec4 SourceSize;
} global;

layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}
)";
}

/** @brief FNV-1a 32-bit content fingerprint for remote field diagnostics. */
unsigned Fnv1a32(const std::string &data)
{
        unsigned hash = 2166136261u;
        for (const char c : data) {
                hash ^= static_cast<unsigned char>(c);
                hash *= 16777619u;
        }
        return hash;
}

/** @brief Whole-file bytes for fingerprinting (binary-safe); capped at
 * kMaxShaderFileBytes (ROB-20) - oversize files report false and the
 * fingerprint is skipped. */
bool ReadWholeFileBytes(const std::string &path, std::string &out)
{
        std::ifstream in(path, std::ios::binary);
        if (!in)
                return false;
        in.seekg(0, std::ios::end);
        const std::streamoff fileBytes = in.tellg();
        in.seekg(0, std::ios::beg);
        if (fileBytes < 0 || fileBytes > kMaxShaderFileBytes)
                return false;
        std::ostringstream buffer;
        buffer << in.rdbuf();
        out = buffer.str();
        return true;
}

/** @brief Non-overlapping substring count. */
int CountOccurrences(const std::string &text, const char *needle)
{
        int count = 0;
        const size_t len = std::strlen(needle);
        for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + len))
                ++count;
        return count;
}

bool ProcessStage(std::string glsl, bool isVertex, int passIndex, int passCount,
    const std::unordered_set<std::string> &knownTextureNames, const std::vector<ParamInfo> &parameters,
    std::vector<std::string> &warnings, StageTranslation &stage, std::string &error)
{
        if (TrimAll(glsl).empty()) {
                stage.present = false;
                return true; // Stage absent in this shader (e.g. fragment-only file).
        }
        // Field round 2: strip comments from the working copy before any
        // structural rewrite. Commented-out uniform blocks ("/* uniform UBO
        // ... */" in the Mega Bezel guest pack) and commented samplers
        // otherwise match the rewriters and hard-fail presets that glslang
        // itself compiles fine. Pragmas were already extracted with their own
        // comment-tolerant rules and the merged fingerprint was taken before
        // this copy, so stripping here is side-effect free. #define lines are
        // preserved: glslang still macro-expands sampling calls in the output.
        glsl = StripComments(glsl);
        if (!RewriteUboBlocks(glsl, isVertex, stage.uniformBlocks, error))
                return false;
        const bool uboPresent = !stage.uniformBlocks.empty();
        if (!RewritePushBlock(glsl, isVertex, uboPresent, stage.uniformBlocks, error))
                return false;
        if (!RewriteSamplers(glsl, isVertex, stage, knownTextureNames, passIndex, passCount, warnings, error))
                return false;
        if (!ValidateBlockMembers(stage.uniformBlocks, parameters, knownTextureNames, warnings))
                return false;
        for (const UniformBlock &block : stage.uniformBlocks) {
                for (const UniformMember &member : block.members) {
                        stage.usesMvp |= member.name == "MVP";
                        stage.usesFrameCount |= member.name == "FrameCount";
                        stage.usesFrameDirection |= member.name == "FrameDirection";
                        stage.usesFrameTime |= member.name == "FrameTime";
                        stage.usesPassIndex |= member.name == "PASSINDEX";
                }
        }
        if (isVertex) {
                EnsureVertexInterface(glsl);
        } else if (glsl.find("void main") == std::string::npos) {
                error = "slang: fragment stage has no main()";
                return false;
        }
        stage.texelFetchCount = CountOccurrences(glsl, "texelFetch");
        stage.textureGatherCount = CountOccurrences(glsl, "textureGather");
        stage.glsl = std::move(glsl);
        stage.present = true;
        return true;
}

bool TranslateSlangImpl(const std::string &shaderPath, const std::unordered_set<std::string> &knownTextureNames,
    int passIndex, int passCount, SlangTranslation &out, const std::vector<ParamInfo> &presetParameters)
{
        out = SlangTranslation {};

        std::string error;
        std::string merged;
        int includeStage = 0; // Mirrors SplitStages: 0 shared, 1 vertex, 2 fragment.
        std::set<std::string> includeVisited[3];
        std::vector<std::string> includeActive;
        if (!ResolveIncludes(shaderPath, 0, includeStage, includeVisited, includeActive, merged, error)) {
                out.error = error;
                return false;
        }
        // Field diagnostics: fingerprint the raw file and the include-resolved
        // source so a remote log can be diffed against known shader-pack versions.
        {
                std::string fileBytes;
                if (ReadWholeFileBytes(shaderPath, fileBytes)) {
                        out.sourceSize = DiagSizeToInt(fileBytes.size());
                        out.sourceHash = Fnv1a32(fileBytes);
                }
                out.mergedSize = DiagSizeToInt(merged.size());
                out.mergedHash = Fnv1a32(merged);
        }

        std::vector<ParamInfo> parameters;
        std::string pragmaName;
        std::string pragmaFormat;
        ExtractPragmas(merged, parameters, pragmaName, pragmaFormat);
        const StageSplit split = SplitStages(merged);
        out.parameters = std::move(parameters);
        // Merge parameter names declared by the preset side (.slangp/.params
        // overlays declare parameters without #pragma lines in the slang). The
        // slang's own #pragma declarations win for description/range metadata.
        for (const ParamInfo &ext : presetParameters) {
                bool known = false;
                for (const ParamInfo &p : out.parameters) {
                        if (p.name.size() == ext.name.size()
                            && std::equal(p.name.begin(), p.name.end(), ext.name.begin(),
                                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
                                known = true;
                                break;
                        }
                }
                if (!known) {
                        ParamInfo info;
                        info.name = ext.name;
                        info.defaultValue = ext.defaultValue;
                        out.parameters.push_back(info);
                }
        }
        out.pragmaName = std::move(pragmaName);
        out.pragmaFormat = std::move(pragmaFormat);

        if (TrimAll(split.shared).empty() && TrimAll(split.vertex).empty() && TrimAll(split.fragment).empty()) {
                out.error = "slang: shader file has no code";
                return false;
        }

        std::vector<std::string> warnings;
        if (!ProcessStage(split.shared + split.vertex, /*isVertex=*/true, passIndex, passCount, knownTextureNames,
                out.parameters, warnings, out.vertex, out.error)) {
                return false;
        }
        if (!ProcessStage(split.shared + split.fragment, /*isVertex=*/false, passIndex, passCount, knownTextureNames,
                out.parameters, warnings, out.fragment, out.error)) {
                return false;
        }
        out.warnings = std::move(warnings);

        // A fragment-only .slang: inject the stock vertex stage.
        if (!out.vertex.present && out.fragment.present) {
                out.vertex.glsl = StockVertexSource();
                out.vertex.present = true;
        }
        if (!out.fragment.present) {
                out.error = "slang: shader file has no fragment stage";
                return false;
        }
        return true;
}

} // namespace

bool TranslateSlang(const std::string &shaderPath, const std::unordered_set<std::string> &knownTextureNames,
    int passIndex, int passCount, SlangTranslation &out, const std::vector<ParamInfo> &presetParameters)
{
        // ROB-20: the load path's only handler - any exception escaping the
        // translate (allocation failure, data-driven throw) converts into the
        // documented failure contract instead of terminating the process.
        try {
                return TranslateSlangImpl(shaderPath, knownTextureNames, passIndex, passCount, out, presetParameters);
        } catch (const std::exception &e) {
                out.error = StrCat("slang: translation failed for ", shaderPath, ": ", e.what());
                return false;
        } catch (...) {
                out.error = StrCat("slang: translation failed for ", shaderPath, ": unknown exception");
                return false;
        }
}

} // namespace preset
} // namespace slv
