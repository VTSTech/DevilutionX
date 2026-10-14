// Standalone logic test for the SLANG_SPEC gap #3/#4 fixes (no SDL needed).
// 1. PassOutput#/PassFeedback# index parsing in FillBlockBytes: the old
//    substr(10) sliced "ck#" for the 12-char PassFeedback prefix and aborted
//    on an uncaught std::invalid_argument; the fix picks the prefix length.
// 2. Uniform array-size parsing in slang_translate: a malformed [N] is now a
//    caught error instead of an uncaught std::invalid_argument/out_of_range.
// 3. Case-insensitive parameter override matching (parser lowercases keys,
//    #pragma identifiers keep case).
// Build: g++ -O2 -Wall -std=c++17 -o test_uniform_names tools/audit/test_uniform_names.cpp
#include <cstdio>
#include <stdexcept>
#include <string>

static int failures = 0;

static void Check(bool ok, const char *label)
{
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", label);
	} else {
		std::printf("ok   %s\n", label);
	}
}

// Copy of the anonymous-namespace helper in preset_runner.cpp.
static bool CaseInsensitiveEqual(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return true;
}

// Mirrors the FillBlockBytes tail: strip "Size", strip a trailing "Feedback"
// form marker, then parse the pass index.
static int ParseSizeIndex(const std::string &member, bool newStyle)
{
	std::string texName = member.substr(0, member.size() - 4); // strip "Size"
	if (texName.size() > 8 && texName.compare(texName.size() - 8, 8, "Feedback") == 0)
		texName = texName.substr(0, texName.size() - 8);
	if (texName.rfind("PassOutput", 0) == 0 || texName.rfind("PassFeedback", 0) == 0) {
		if (newStyle) {
			const size_t prefixLen = texName.rfind("PassFeedback", 0) == 0 ? 12 : 10;
			return std::stoi(texName.substr(prefixLen));
		}
		return std::stoi(texName.substr(10)); // the buggy form
	}
	return -1;
}

// Mirrors the guarded uniform array-size parse.
static int ParseArraySize(const std::string &countText, bool &errored)
{
	errored = false;
	try {
		const int value = std::stoi(countText);
		if (value <= 0 || value > 64) {
			errored = true;
			return 0;
		}
		return value;
	} catch (const std::exception &) {
		errored = true;
		return 0;
	}
}

int main()
{
	// --- 1. PassOutput#/PassFeedback# index parse (gap #4) ---
	bool oldThrew = false;
	try {
		(void)ParseSizeIndex("PassFeedback3Size", false);
	} catch (const std::exception &) {
		oldThrew = true;
	}
	Check(oldThrew, "old substr(10) aborts on PassFeedback#Size (documents the bug)");

	bool ok = true;
	for (int i = 0; i <= 12; ++i) {
		ok = ok && ParseSizeIndex("PassOutput" + std::to_string(i) + "Size", true) == i;
		ok = ok && ParseSizeIndex("PassFeedback" + std::to_string(i) + "Size", true) == i;
		ok = ok && ParseSizeIndex("PassFeedback" + std::to_string(i) + "FeedbackSize", true) == i;
	}
	Check(ok, "new parse: PassOutput#/PassFeedback# and FeedbackSize forms, idx 0..12");

	// --- 2. Uniform array-size guard (same crash class) ---
	bool errored = false;
	Check(ParseArraySize("4", errored) == 4 && !errored, "array size [4] parses");
	Check(ParseArraySize("64", errored) == 64 && !errored, "array size [64] parses");
	Check(ParseArraySize("x2", errored) == 0 && errored, "array size [x2] -> loud error");
	Check(ParseArraySize("", errored) == 0 && errored, "unsized [] -> loud error");
	Check(ParseArraySize("99999999999999", errored) == 0 && errored, "array size overflow -> loud error");

	// --- 3. Case-insensitive override application (gap #3) ---
	Check(CaseInsensitiveEqual("abs_apect_mult", "ABS_APECT_mult"), "mixed-case override matches pragma");
	Check(CaseInsensitiveEqual("WARP_X", "warp_x"), "lowercased key matches uppercase pragma");
	Check(CaseInsensitiveEqual("WARP_X", "WARP_X"), "exact case still matches");
	Check(!CaseInsensitiveEqual("WARP_X", "WARP_Y"), "distinct params do not match");
	Check(!CaseInsensitiveEqual("param", "param2"), "length mismatch does not match");
	Check(!CaseInsensitiveEqual("param\x80", "param\xff"), "non-ASCII bytes compared as unsigned");

	if (failures == 0) {
		std::printf("PASS: all checks passed\n");
		return 0;
	}
	std::printf("FAIL: %d check(s) failed\n", failures);
	return 1;
}
