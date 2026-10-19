#!/usr/bin/env python3
"""merge-plan-r4 branch builder (see merge-plan-r4.md).

Creates one branch per individual change, straight from the fork's payload
blobs, with EOL normalization against upstream and attribution stamps applied
to the scaler PR's new source files. Every branch = exactly one commit.

Usage (inside a clone of VTSTech/DevilutionX):
    git remote add upstream https://github.com/diasurgical/DevilutionX.git  # if absent
    git fetch upstream master
    git fetch origin libslang-vk
    python3 build_r4_branches.py [--repo PATH] [--worktree-root PATH]
                                 [--upstream upstream/master] [--force] [--verify-only]

Payload source: origin/libslang-vk at its CURRENT tip - the CI-green,
hardware-validated tree (first refresh basis: 29d1fe278, libslang-vk M4.05
pin, 2-arg Chain::resize, WinMain SDL_main.h include, EOL-normalized).
Re-running with --force refreshes the branches to whatever the tip carries;
force-push the pr/* branches afterwards. docs/GPU_SHADERS.md,
docs/SLANG_SPEC.md and tools/audit_slang_presets.py are lean-dropped from
libslang-vk and still come from the r3 snapshot (6dc2a8713).

Branches produced:
    pr/1-sdl3-build-fixes        (base: upstream/master)
    pr/2-wav-audio-robustness    (base: upstream/master)
    pr/3-robustness-guards       (base: upstream/master)
    pr/4-test-missing-asset-guards (base: upstream/master)
    pr/5-gpu-scaler              (base: pr/1-sdl3-build-fixes  <-- set GitHub PR base to it)
    pr/6-sdl3-main-glue          (base: upstream/master)
"""
import argparse
import difflib
import os
import shutil
import subprocess
import sys

FORK = "origin/libslang-vk"   # validated payload source (see docstring)
FORK_R3 = "6dc2a8713"         # r3 snapshot: still supplies the 3 lean-dropped docs/tools files
BR1 = "pr/1-sdl3-build-fixes"

ATTR_CPP = [
    "// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)",
    "// AI Assisted by Super-Z (GLM-5.3-Flash)",
    "// License: MIT",
]

PR2_BULLET = (
    "- Recover 1990s-era WAV sound effects that SDL3_mixer 3.2.0 cannot parse "
    "(zero-length RIFF chunks): skip empty chunks in the vendored decoder and "
    "add an SDL_LoadWAV_IO-based fallback (DEVILUTIONX_WAV_FALLBACK, default ON); "
    "undecodable sounds now log an error instead of aborting the game"
)
PR5_BULLET = (
    "- Optional Vulkan GPU scaler: render through libretro slang shader presets "
    "via the MIT-licensed libslang-vk library (new USE_GPU_SCALER CMake option, "
    "on by default for SDL3 builds, graceful fallback to stock rendering)"
)

GMOCK_BLOCK = [
    "  # These standalone tests include <gmock/gmock.h> but only linked gtest_main,",
    "  # which carries no gmock include dirs - they only ever built where system",
    "  # gtest headers sat on the default include path. Link GTest::gmock when the",
    "  # dependency tree provides it (vendored googletest always does; system",
    "  # FindGTest defines it when libgmock was found) so the suite builds anywhere.",
    "  if(TARGET GTest::gmock)",
    "    target_link_libraries(${test_target} PRIVATE GTest::gmock)",
    "  endif()",
]

OPTIONS_TEST_BLOCK = [
    "# Shader-filter option layer tests (stale-value self-heal, custom-index",
    "# mapping, ini round-trip) and the corrupt-ini recovery (preserve-and-",
    "# restart), headless. The options machinery links standalone via",
    "# libdevilutionx_options; SDL is needed only for SDL_SetBasePath/SDL_Init",
    "# (no GPU, no window).",
    "target_link_dependencies(options_test",
    "  PRIVATE",
    "  DevilutionX::SDL",
    "  libdevilutionx_options",
    "  libdevilutionx_paths",
    "  libdevilutionx_assets",
    "  tl",
    "  unordered_dense::unordered_dense",
    "  app_fatal_for_testing",
    "  language_for_testing",
    ")",
]

MAIN_CPP_INSERT = [
    "// SDL3: SDL.h no longer includes SDL_main.h. Including it here renames main()",
    "// to SDL_main() and emits the WinMain()/main() entry points that call into",
    "// SDL_RunApp(); without it, WIN32 GUI-subsystem executables fail to link",
    "// (LNK2019 unresolved external symbol WinMain).",
    "#include <SDL3/SDL_main.h>",
]


# ---------------------------------------------------------------- git helpers
class Repo:
    def __init__(self, path: str):
        self.path = path

    def git(self, *args: str, check: bool = True) -> bytes:
        r = subprocess.run(["git", "-C", self.path, *args],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if check and r.returncode != 0:
            raise RuntimeError(f"git {' '.join(args)}: {r.stderr.decode(errors='replace')}")
        return r.stdout

    def blob(self, rev: str, path: str) -> bytes:
        return self.git("show", f"{rev}:{path}", check=False)

    def has_blob(self, rev: str, path: str) -> bool:
        return self.git("cat-file", "-e", f"{rev}:{path}", check=False).returncode == 0


# ------------------------------------------------------------- text helpers
def split_lines(data: bytes):
    return data.splitlines(keepends=True)


def strip_eol(line: bytes) -> bytes:
    return line.rstrip(b"\r\n")


def dominant_eol(data: bytes) -> bytes:
    crlf = data.count(b"\r\n")
    lf = data.count(b"\n") - crlf
    return b"\r\n" if crlf >= lf else b"\n"


def render(lines: list[bytes]) -> bytes:
    return b"".join(lines)


def normalize_to_upstream(up: bytes, fk: bytes) -> bytes:
    """Rebuild the fork content on top of the upstream file's exact bytes.

    Unchanged lines keep upstream's original EOLs; added/replaced lines take
    the upstream file's dominant EOL. Result: `git diff` shows only true
    content changes, never EOL churn.
    """
    up_l, fk_l = split_lines(up), split_lines(fk)
    eol = dominant_eol(up)
    sm = difflib.SequenceMatcher(
        a=[strip_eol(l) for l in up_l], b=[strip_eol(l) for l in fk_l], autojunk=False)
    out: list[bytes] = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            out.extend(up_l[i1:i2])
        elif tag in ("replace", "insert"):
            out.extend(strip_eol(l) + eol for l in fk_l[j1:j2])
    return render(out)


def insert_once(lines: list[bytes], *, after: bytes = None, before: bytes = None,
                new_lines: list[bytes], eol: bytes, prefix: bool = False) -> list[bytes]:
    """Insert new_lines after/before the unique line whose stripped content
    equals (or, with prefix=True, starts with) `after`/`before`."""
    if (after is None) == (before is None):
        raise ValueError("exactly one of after/before required")
    needle = (after if after is not None else before).lstrip()
    hits = []
    for i, l in enumerate(lines):
        c = strip_eol(l).lstrip()
        if (c.startswith(needle) if prefix else c == needle):
            hits.append(i)
    if len(hits) != 1:
        raise RuntimeError(f"anchor {needle!r}: expected 1 occurrence, found {len(hits)}")
    i = hits[0] + (1 if after is not None else 0)
    coerced = [(n if isinstance(n, bytes) else n.encode()) for n in new_lines]
    return lines[:i] + [n + eol for n in coerced] + lines[i:]


def remove_lines(lines: list[bytes], prefix: bytes) -> list[bytes]:
    kept = [l for l in lines if not strip_eol(l).startswith(prefix)]
    if len(kept) == len(lines):
        raise RuntimeError(f"remove_lines: no line starts with {prefix!r}")
    return kept


def changelog_add_bullet(data: bytes, section: bytes, subsection: bytes, bullet: str) -> bytes:
    """Append `bullet` to the bullet block under the FIRST `<section>` heading's
    `<subsection>` (Keep-a-Changelog style, upstream docs/CHANGELOG.md)."""
    lines = split_lines(data)
    eol = dominant_eol(data)
    sec_idx = [i for i, l in enumerate(lines) if strip_eol(l) == section]
    if not sec_idx:
        raise RuntimeError(f"changelog: section {section!r} not found")
    start = sec_idx[0]
    try:
        sub_idx = next(i for i in range(start + 1, len(lines))
                       if strip_eol(lines[i]) == subsection)
    except StopIteration:
        raise RuntimeError(f"changelog: subsection {subsection!r} not found after {section!r}")
    j = sub_idx + 1
    while j < len(lines) and strip_eol(lines[j]).startswith(b"- "):
        j += 1
    return render(lines[:j] + [bullet.encode() + eol] + lines[j:])


def stamp(data: bytes, token: bytes, after_shebang: bool = False) -> bytes:
    """Prepend the attribution lines with the given comment token.

    Idempotent: fork-tip blobs already carry the stamps (they were validated
    on CI/hardware there), so detect and pass through unchanged.
    """
    lines = split_lines(data)
    if any(b"Written by VTSTech" in strip_eol(l) for l in lines[:6]):
        return data
    eol = dominant_eol(data)
    attr = [t + b" " + line + eol for t in (token,) for line in
            [b"Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)",
             b"AI Assisted by Super-Z (GLM-5.3-Flash)",
             b"License: MIT"]]
    if after_shebang:
        if not lines or not strip_eol(lines[0]).startswith(b"#!"):
            raise RuntimeError("stamp: expected shebang first line")
        return render(lines[:1] + attr + lines[1:])
    return render(attr + lines)


# ------------------------------------------------------------ PR definitions
def build_prs(repo: Repo, upstream: str) -> list[dict]:
    up = lambda p: repo.blob(upstream, p)
    fk = lambda p: repo.blob(FORK, p)
    eol_of = lambda data: dominant_eol(data)

    up_tests = up("CMake/Tests.cmake")
    tests_eol = eol_of(up_tests)
    pr1_tests = insert_once(split_lines(up_tests),
                            after=b"target_link_libraries(${test_target} PRIVATE GTest::gtest_main",
                            new_lines=GMOCK_BLOCK, eol=tests_eol, prefix=True)
    up_cmake = up("CMakeLists.txt")
    cmake_eol = eol_of(up_cmake)
    wav_line = [l for l in split_lines(repo.blob(FORK, "CMakeLists.txt"))
                if strip_eol(l).startswith(b"option(DEVILUTIONX_WAV_FALLBACK")]
    assert len(wav_line) == 1, "wav option line not found in fork-tip CMakeLists.txt"
    pr2_cmake = insert_once(split_lines(up_cmake),
                            before=b"set(STREAM_ALL_AUDIO_MIN_FILE_SIZE",
                            new_lines=[strip_eol(wav_line[0])], eol=cmake_eol, prefix=True)
    fk_cmake_lines = split_lines(repo.blob(FORK, "CMakeLists.txt"))
    pr5_cmake_src = remove_lines(fk_cmake_lines, b"option(DEVILUTIONX_WAV_FALLBACK")

    pr5_tests_base = None  # filled at runtime from the pr/1 branch

    def pr5_tests() -> bytes:
        base_blob = repo.blob(BR1, "CMake/Tests.cmake")
        lines = insert_once(split_lines(base_blob),
                            after=b"multi_logging_test",
                            new_lines=[b"  options_test"], eol=tests_eol)
        lines = insert_once(lines,
                            before=b"target_link_dependencies(light_render_benchmark",
                            new_lines=OPTIONS_TEST_BLOCK, eol=tests_eol, prefix=True)
        return render(lines)

    def main_cpp() -> bytes:
        return render(insert_once(split_lines(up("Source/main.cpp")),
                                  after=b"#include <SDL3/SDL.h>",
                                  new_lines=MAIN_CPP_INSERT, eol=eol_of(up("Source/main.cpp"))))

    TESTS8 = ["char_panel", "game_menu", "inventory_ui", "panel_state",
              "spell_ui", "stash", "store_transaction", "visual_store"]

    def norm(p): return normalize_to_upstream(up(p), fk(p))

    fk_r3 = lambda p: repo.blob(FORK_R3, p)  # lean-dropped docs/tools source

    def stamped_add(p, token: bytes, after_shebang=False):
        return stamp(fk(p), token, after_shebang)

    presets = ["good/dupinclude.slangp", "good/feedback.slangp", "good/history.slangp",
               "good/lut.slangp", "good/multipass.slangp", "good/single.slangp",
               "good/shaders/alias_final.slang", "good/shaders/blend.slang",
               "good/shaders/dupinclude.slang", "good/shaders/feedback.slang",
               "good/shaders/guarded_texture_header.slang", "good/shaders/history.slang",
               "good/shaders/lut_mix.slang", "good/shaders/passthrough.slang",
               "good/textures/lut.png", "malformed/bad_count.slangp",
               "malformed/bad_index.slangp", "malformed/bad_slang.slangp",
               "malformed/empty.slangp", "malformed/missing_shader.slangp",
               "malformed/shaders/broken.slang"]

    prs = []
    prs.append(dict(
        branch=BR1, base=upstream,
        title="Fix SDL3 dependency builds: vendor SDL3_image png/zlib, pin BUILD_SHARED_LIBS, declare the sdl3 vcpkg feature",
        body=("SDL3_image: SDLIMAGE_VENDORED with the git fetch (png/zlib/jpeg submodules) and "
              "PNG_SHARED OFF, so PNG works in cross builds without a runtime dlopen'd DLL.\n"
              "Dependencies: pin BUILD_SHARED_LIBS=OFF at the root (SDL_image/SDL_mixer's "
              "dependent option leaked ON into every later dependency) and provide zlib inside "
              "the USE_SDL3 branch by reusing SDL3_image's vendored copy (CMP0002).\n"
              "libpng: dynamic target is png_shared.\n"
              "vcpkg.json: declare the sdl3 feature that VcPkgManifestFeatures.cmake already "
              "appends.\n"
              "Tests: link GTest::gmock for standalone tests that include gmock."),
        mods={
            "3rdParty/SDL3_image/CMakeLists.txt": norm("3rdParty/SDL3_image/CMakeLists.txt"),
            "CMake/Dependencies.cmake": norm("CMake/Dependencies.cmake"),
            "3rdParty/libpng/CMakeLists.txt": norm("3rdParty/libpng/CMakeLists.txt"),
            "vcpkg.json": norm("vcpkg.json"),
            "CMake/Tests.cmake": render(pr1_tests),
        },
        adds={}, chmod=[]))

    prs.append(dict(
        branch="pr/2-wav-audio-robustness", base=upstream,
        title="Audio: recover WAVs SDL3_mixer cannot parse (zero-length RIFF chunks) and stop fatal-erroring on undecodable sounds",
        body=("1996-era WAVs (original Diablo SFX) contain zero-length RIFF chunks that "
              "SDL3_mixer 3.2.0 rejects, after which sound_file_load() called app_fatal().\n"
              "Layers: patched vendored WAV decoder (skips empty chunks), DEVILUTIONX_WAV_FALLBACK "
              "(SDL_LoadWAV_IO -> canonical RIFF re-encode retry, default ON), and graceful "
              "degradation: undecodable sounds log an error and playback skips them.\n"
              "MpqArchive::path() names the archive in the failure diagnostics."),
        mods={
            "docs/CHANGELOG.md": changelog_add_bullet(up("docs/CHANGELOG.md"),
                                                      b"### Bug Fixes", b"#### Graphics / Audio", PR2_BULLET),
            "CMakeLists.txt": render(pr2_cmake),
            "CMake/Definitions.cmake": norm("CMake/Definitions.cmake"),
            "3rdParty/SDL3_mixer/CMakeLists.txt": norm("3rdParty/SDL3_mixer/CMakeLists.txt"),
            "3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch": fk(
                "3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch"),
            "Source/engine/sound.cpp": norm("Source/engine/sound.cpp"),
            "Source/utils/soundsample.cpp": norm("Source/utils/soundsample.cpp"),
            "Source/utils/soundsample.h": norm("Source/utils/soundsample.h"),
            "Source/mpq/mpq_reader.hpp": norm("Source/mpq/mpq_reader.hpp"),
        },
        adds={}, chmod=[]))

    prs.append(dict(
        branch="pr/3-robustness-guards", base=upstream,
        title="Guard menu-time item-cursor art access and fix the screenshot failure log format",
        body=("GetInvItemSize() dereferenced the CLX data of item-cursor art that is only "
              "loaded inside a game session; building the virtual gamepad's potion strip in "
              "the main menu (e.g. while changing shader settings) crashed. Report zero size "
              "and skip instead. Also fix LogError(\"...at {}: {}\") which had one placeholder "
              "for two arguments."),
        mods={
            "Source/cursor.cpp": norm("Source/cursor.cpp"),
            "Source/controls/touch/renderers.cpp": norm("Source/controls/touch/renderers.cpp"),
            "Source/capture.cpp": norm("Source/capture.cpp"),
        },
        adds={}, chmod=[]))

    pr4_mods = {"test/ui_test.hpp": norm("test/ui_test.hpp")}
    for t in TESTS8:
        pr4_mods[f"test/{t}_test.cpp"] = norm(f"test/{t}_test.cpp")
    prs.append(dict(
        branch="pr/4-test-missing-asset-guards", base=upstream,
        title="Skip UI test setup/teardown halves when MPQ fixtures are missing",
        body=("UITest::SetUp() GTEST_SKIPs without MPQ fixtures, but TearDown() and the derived "
              "SetUp() halves still ran cleanup/init on never-initialised engine state. Add "
              "MissingAssets() and early returns so asset-less environments skip cleanly."),
        mods=pr4_mods, adds={}, chmod=[]))

    pr5_adds = {
        "Source/engine/render/gpu/gpu_scaler.cpp": stamped_add("Source/engine/render/gpu/gpu_scaler.cpp", b"//"),
        "Source/engine/render/gpu/gpu_scaler.hpp": stamped_add("Source/engine/render/gpu/gpu_scaler.hpp", b"//"),
        "Source/engine/render/gpu/gpu_log_sanitize.hpp": stamped_add("Source/engine/render/gpu/gpu_log_sanitize.hpp", b"//"),
        "Packaging/linux/clear_shader_cache.sh": stamped_add("Packaging/linux/clear_shader_cache.sh", b"#", True),
        "Packaging/linux/install_shaders.sh": stamped_add("Packaging/linux/install_shaders.sh", b"#", True),
        "Packaging/windows/clear_gpu-scaler.log.cmd": stamped_add("Packaging/windows/clear_gpu-scaler.log.cmd", b"rem"),
        "Packaging/windows/clear_shader_cache.cmd": stamped_add("Packaging/windows/clear_shader_cache.cmd", b"rem"),
        "Packaging/windows/install_git.cmd": stamped_add("Packaging/windows/install_git.cmd", b"rem"),
        "Packaging/windows/install_shaders.cmd": stamped_add("Packaging/windows/install_shaders.cmd", b"rem"),
        "docs/GPU_SHADERS.md": fk_r3("docs/GPU_SHADERS.md"),
        "docs/SLANG_SPEC.md": fk_r3("docs/SLANG_SPEC.md"),
        "test/options_test.cpp": stamped_add("test/options_test.cpp", b"//"),
        "tools/audit_slang_presets.py": stamp(repo.blob(FORK_R3, "tools/audit_slang_presets.py"), b"#"),
    }
    for rel in presets:
        pr5_adds[f"test/fixtures/preset_smoke/{rel}"] = fk(f"test/fixtures/preset_smoke/{rel}")

    pr5_mods = {
        "docs/CHANGELOG.md": changelog_add_bullet(up("docs/CHANGELOG.md"),
                                                  b"### Features", b"#### Graphics / Audio", PR5_BULLET),
        "CMakeLists.txt": normalize_to_upstream(up_cmake, render(pr5_cmake_src)),
        "Source/CMakeLists.txt": norm("Source/CMakeLists.txt"),
        "CMake/Tests.cmake": pr5_tests,   # callable: needs the pr/1 branch to exist
        "Source/options.cpp": norm("Source/options.cpp"),
        "Source/options.h": norm("Source/options.h"),
        "Source/utils/display.cpp": norm("Source/utils/display.cpp"),
        "Source/engine/dx.cpp": norm("Source/engine/dx.cpp"),
        "Source/engine/render/scrollrt.cpp": norm("Source/engine/render/scrollrt.cpp"),
        "Source/interfac.cpp": norm("Source/interfac.cpp"),
        "Source/DiabloUI/diabloui.cpp": norm("Source/DiabloUI/diabloui.cpp"),
        "Source/DiabloUI/settingsmenu.cpp": norm("Source/DiabloUI/settingsmenu.cpp"),
    }
    prs.append(dict(
        branch="pr/5-gpu-scaler", base=BR1,
        title="Optional Vulkan GPU scaler: slang shader presets via libslang-vk (USE_GPU_SCALER)",
        body=("Render the game through libretro slang shader presets using the MIT-licensed "
              "libslang-vk library (FetchContent, SHA-pinned; volk loads Vulkan at runtime, so "
              "no Vulkan SDK is needed at build time).\n"
              "The adapter (Source/engine/render/gpu) handles window/swapchain lifecycle, "
              "present-path integration, filter OSD and graceful fallback to the stock renderer "
              "on any GPU-path failure. Options/settings UI add a shader-filter entry with a "
              "preset browser; corrupt-ini recovery preserves the file and restarts with "
              "defaults. Packaging scripts install/clear shader caches; docs + a preset QA "
              "corpus (tools/audit_slang_presets.py) are included.\n"
              "Stacked on pr/1-sdl3-build-fixes, which the Windows/vendored-SDL3 configure path "
              "requires."),
        mods=pr5_mods, adds=pr5_adds,
        chmod=["Packaging/linux/clear_shader_cache.sh", "Packaging/linux/install_shaders.sh"]))

    prs.append(dict(
        branch="pr/6-sdl3-main-glue", base=upstream,
        title="SDL3: include SDL3/SDL_main.h in main.cpp so WIN32 GUI builds link",
        body=("SDL3's SDL.h no longer includes SDL_main.h (SDL3 migration change). Without it, "
              "main() is not renamed to SDL_main() and no WinMain entry point is emitted, so "
              "add_executable(devilutionx WIN32) fails with LNK2019 (unresolved WinMain). "
              "SDL_main.h is a no-op on Linux/macOS desktop builds."),
        mods={"Source/main.cpp": main_cpp()}, adds={}, chmod=[]))

    return prs


# ----------------------------------------------------------------- execution
EXPECTED_FILES = {
    BR1: ["3rdParty/SDL3_image/CMakeLists.txt", "3rdParty/libpng/CMakeLists.txt",
          "CMake/Dependencies.cmake", "CMake/Tests.cmake", "vcpkg.json"],
    "pr/2-wav-audio-robustness": [
        "3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch",
        "3rdParty/SDL3_mixer/CMakeLists.txt", "CMake/Definitions.cmake", "CMakeLists.txt",
        "Source/engine/sound.cpp", "Source/mpq/mpq_reader.hpp",
        "Source/utils/soundsample.cpp", "Source/utils/soundsample.h", "docs/CHANGELOG.md"],
    "pr/3-robustness-guards": ["Source/capture.cpp", "Source/cursor.cpp",
                               "Source/controls/touch/renderers.cpp"],
    "pr/4-test-missing-asset-guards": ["test/ui_test.hpp"] + [
        f"test/{t}_test.cpp" for t in ["char_panel", "game_menu", "inventory_ui",
                                       "panel_state", "spell_ui", "stash",
                                       "store_transaction", "visual_store"]],
    "pr/5-gpu-scaler": None,  # too many; checked by count
    "pr/6-sdl3-main-glue": ["Source/main.cpp"],
}
PR5_COUNT = 34 + 12


def verify(repo: Repo, pr: dict) -> bool:
    ok = True
    base, br = pr["base"], pr["branch"]
    diff = repo.git("diff", "--name-status", f"{base}..{br}").decode()
    n_del = sum(1 for l in diff.splitlines() if l.startswith("D"))
    files = [l.split("\t", 1)[1] for l in diff.splitlines() if l.strip()]
    if n_del:
        print(f"  FAIL {br}: {n_del} deletions"); ok = False
    if any(f.startswith(".github/") for f in files):
        print(f"  FAIL {br}: .github/ present"); ok = False
    n = repo.git("rev-list", "--count", f"{base}..{br}").decode().strip()
    if n != "1":
        print(f"  FAIL {br}: {n} commits (want 1)"); ok = False
    branding = repo.git("diff", f"{base}..{br}", "--", "Source", check=False)
    if b"github.com/VTSTech" in branding:
        print(f"  FAIL {br}: branding string in Source diff"); ok = False
    exp = EXPECTED_FILES.get(br)
    if exp is not None:
        if sorted(files) != sorted(exp):
            print(f"  FAIL {br}: file set mismatch\n    got: {sorted(files)}")
            ok = False
    elif len(files) != PR5_COUNT:
        print(f"  FAIL {br}: {len(files)} files (want {PR5_COUNT})"); ok = False
    return ok


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.getcwd())
    ap.add_argument("--worktree-root", default=None)
    ap.add_argument("--upstream", default="upstream/master")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--verify-only", action="store_true")
    args = ap.parse_args()

    repo = Repo(os.path.abspath(args.repo))
    wt_root = args.worktree_root or os.path.join(os.path.dirname(repo.path), "r4-worktrees")

    # Preflight
    for rev in (args.upstream, FORK, FORK_R3):
        repo.git("rev-parse", "--verify", f"{rev}^{{commit}}")
    prs = build_prs(repo, args.upstream)

    if args.verify_only:
        good = all(verify(repo, pr) for pr in prs)
        print("VERIFY:", "OK" if good else "FAILURES")
        return 0 if good else 1

    os.makedirs(wt_root, exist_ok=True)
    failures = 0
    for pr in prs:
        br, base = pr["branch"], pr["base"]
        print(f"== {br} (base {base}) ==")
        if repo.git("branch", "--list", br, check=False).strip():
            if not args.force:
                print("  SKIP: branch exists (use --force to rebuild)")
                continue
            repo.git("branch", "-D", br)
        if base != args.upstream and not repo.git(
                "rev-parse", "--verify", "-q", base, check=False):
            raise RuntimeError(f"base branch missing: {base}")
        wt = os.path.join(wt_root, br.replace("/", "__"))
        if os.path.exists(wt):
            shutil.rmtree(wt)
        repo.git("worktree", "add", "--detach", wt, base)
        try:
            for path, content in pr["adds"].items():
                dst = os.path.join(wt, path)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(content() if callable(content) else content)
            for path, content in pr["mods"].items():
                dst = os.path.join(wt, path)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(content() if callable(content) else content)
            wgit = lambda *a: subprocess.run(["git", "-C", wt, *a], check=True,
                                             stdout=subprocess.DEVNULL,
                                             stderr=subprocess.PIPE)
            wgit("add", "-A")
            for p in pr["chmod"]:
                wgit("update-index", "--chmod=+x", p)
            env_user = ["-c", "user.name=VTSTech", "-c", "user.email=veritas@vts-tech.org"]
            subprocess.run(["git", "-C", wt, *env_user, "commit",
                            "-m", pr["title"], "-m", pr["body"]], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            # point the branch in the main repo at the worktree commit
            tip = subprocess.run(["git", "-C", wt, "rev-parse", "HEAD"],
                                 check=True, stdout=subprocess.PIPE).stdout.strip()
            repo.git("update-ref", f"refs/heads/{br}", tip.decode())
            stat = repo.git("diff", "--stat", f"{base}..{br}").decode().splitlines()[-1]
            print(f"  {stat}")
            if not verify(repo, pr):
                failures += 1
        finally:
            subprocess.run(["git", "-C", repo.path, "worktree", "remove", "--force", wt],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("DONE:", "all branches verified" if not failures else f"{failures} branch(es) failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
