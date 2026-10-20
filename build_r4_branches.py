#!/usr/bin/env python3
"""merge-plan-r4 branch builder (see merge-plan-r4.md).

Creates one branch per individual change, straight from the fork's payload
blobs, with EOL normalization against upstream and attribution stamps applied
to the scaler PR's new source files. Every branch = exactly one commit.

Usage (inside a clone of VTSTech/DevilutionX-libslang-vk):
    git remote add upstream https://github.com/diasurgical/DevilutionX.git  # if absent
    python3 build_r4_branches.py [--repo PATH] [--worktree-root PATH]
                                 [--upstream upstream/master] [--force] [--verify-only]
    python3 build_r4_branches.py --recut          # refresh: delete old local plan branches, cut new
    python3 build_r4_branches.py --recut --push   # ... and force-push (lease) them to origin
    python3 build_r4_branches.py --recut --prune  # ... and drop retired/superseded branches (local + origin)
    python3 build_r4_branches.py --check-prs      # report upstream PR state for the plan branches

    --recut fetches origin/libslang-vk + upstream/master first, then rebuilds
    every pr/* branch from the fetched tip (implies --force). Branches whose
    derived tree AND base are unchanged keep their existing tip, so re-runs
    only rewrite what actually moved and --push does not spam force-pushes.

Payload source: origin/libslang-vk at its CURRENT tip - the hardware-
validated tree the user tests on. (fix/sdl-compat-scaler-decl is merged -
the tip carries the qualified gpu_scaler calls; sdl_compat.h is still
sanitized on the way out as a belt-and-braces gate, see below.)

PR status (2026-10-20): #8750 (pr/1), #8753 (pr/4), #8754 (pr/5) and
#8755 (pr/6) are open; #8751 (pr/2) and #8752 (pr/3) were closed by the
author and are RETIRED from this plan - --prune removes both branches
(local and origin when they still exist there). Check the live state of
the four plan PRs with --check-prs before a --push/--prune run:
  * pr/2: upstream sanitizes the broken audio data itself now (the merged
    hellfire.mpq audio sanitizer), so the WAV-fallback/decoder-robustness
    payload is not wanted upstream - the same layer is being dropped from
    the fork tip as well ("Drop the WAV fallback/robustness layer").
  * pr/3: review (StephenCWills) showed the menu-time item-cursor crash was
    fork-side - the scaler's renderer-recreation path called
    InitVirtualGamepadGFX() outside a game session; the fork now gates that
    on gbRunGame (tip "Update display.cpp"), and the remaining capture.cpp
    LogError one-liner is left for upstream to fix.

As of 994bdb57a the tip carries the two out-of-plan fixes already squashed
on the branch, and the recut folds both into pr/5:
  * 694109544 nonstatic MSVC implib fix -> rides in pr/5's CMakeLists.txt
    (slangvk-shared-implib/ import-library split, DLL named libslangvk.dll);
  * 994bdb57a scaler mouse-mapping fix -> rides in pr/5 via gpu_scaler.* plus
    two NEW pr/5 mods: Source/utils/display.h (OutputToLogical /
    LogicalToOutput hooks) and Source/utils/sdl_compat.h
    (SDLC_ConvertEventToRenderCoordinates hook). sdl_compat.h is emitted with
    its two hook call sites QUALIFIED (devilution::gpu_scaler::...) so the
    Linux CI failure "'gpu_scaler' has not been declared"
    (sdl_compat.h:430/432) cannot recur on the PR branches even before the
    fork branch itself is fixed.

docs/GPU_SHADERS.md, docs/SLANG_SPEC.md and tools/audit_slang_presets.py are
lean-dropped from libslang-vk and still come from the r3 snapshot (29d1fe278).

Retired branches, removed by --prune (local + origin when still present
there): pr/2-wav-audio-robustness and pr/3-robustness-guards (closed PRs,
see above), plus the fix-stage branches pr/nonstatic-slangvk-implib,
pr/scaler-mouse-mapping and fix/sdl-compat-scaler-decl (their content lives
on the tip / inside pr/5).

Branches produced:
    pr/1-sdl3-build-fixes        (base: upstream/master)
    pr/4-test-missing-asset-guards (base: upstream/master)
    pr/5-gpu-scaler              (base: pr/1-sdl3-build-fixes  <-- set GitHub PR base to it)
    pr/6-sdl3-main-glue          (base: upstream/master)
"""
import argparse
import difflib
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

FORK = "origin/libslang-vk"   # validated payload source (see docstring)
FORK_R3 = "29d1fe27"         # r3 snapshot: still supplies the 3 lean-dropped docs/tools files
BR1 = "pr/1-sdl3-build-fixes"

ATTR_CPP = [
    "// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)",
    "// AI Assisted by Super-Z (GLM-5.3-Flash)",
    "// License: MIT",
]

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
        return subprocess.run(["git", "-C", self.path, "cat-file", "-e", f"{rev}:{path}"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                              ).returncode == 0


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


def sanitize_sdl_compat(data: bytes) -> bytes:
    """Qualify the two gpu_scaler::MapWindowToGame() call sites inside the
    global-scope SDLC_ConvertEventToRenderCoordinates() hook.

    The hook sits outside namespace devilution, so unqualified lookup cannot
    reach devilution::gpu_scaler even though this header carries the
    forward-declaration block - TUs that include sdl_compat.h without
    display.h (e.g. engine/assets.cpp) fail with "'gpu_scaler' has not been
    declared" (Linux CI, sdl_compat.h:430/432). Idempotent: already-qualified
    input passes through unchanged.
    """
    qual = b"devilution::gpu_scaler::MapWindowToGame("
    raw = b"gpu_scaler::MapWindowToGame("
    if qual in data:
        if data.count(qual) != 2 or data.count(raw) != 2:
            raise RuntimeError("sdl_compat.h: inconsistent qualified call sites")
        return data
    if b"bool MapWindowToGame(float *x, float *y);" not in data:
        raise RuntimeError("sdl_compat.h: forward-declaration block missing")
    if data.count(raw) != 2:
        raise RuntimeError(
            f"sdl_compat.h: expected 2 unqualified calls, found {data.count(raw)}")
    return data.replace(raw, qual)


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
    fk_cmake_lines = split_lines(repo.blob(FORK, "CMakeLists.txt"))
    # Tolerant filter: the fork tip no longer carries the retired WAV option
    # (pr/2 dropped), but keep working against older tips that still have it.
    pr5_cmake_src = [l for l in fk_cmake_lines
                     if not strip_eol(l).startswith(b"option(DEVILUTIONX_WAV_FALLBACK")]

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
        "Source/utils/display.h": norm("Source/utils/display.h"),
        "Source/utils/sdl_compat.h": sanitize_sdl_compat(fk("Source/utils/sdl_compat.h")),
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
              "on any GPU-path failure. When the scaler owns the window there is no SDL "
              "renderer for SDL to convert events with, so mouse/highlight coordinates are "
              "mapped between window and game space through the scaler's last present rect "
              "(MapWindowToGame; display.h/sdl_compat.h hooks).\n"
              "Options/settings UI add a shader-filter entry with a "
              "preset browser; corrupt-ini recovery preserves the file and restarts with "
              "defaults. Packaging scripts install/clear shader caches; docs + a preset QA "
              "corpus (tools/audit_slang_presets.py) are included. The shared slangvk build "
              "moves its MSVC import library aside (slangvk-shared-implib/) so nonstatic "
              "builds no longer emit duplicate slangvk.lib rules.\n"
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
    "pr/4-test-missing-asset-guards": ["test/ui_test.hpp"] + [
        f"test/{t}_test.cpp" for t in ["char_panel", "game_menu", "inventory_ui",
                                       "panel_state", "spell_ui", "stash",
                                       "store_transaction", "visual_store"]],
    "pr/5-gpu-scaler": None,  # too many; checked by count
    "pr/6-sdl3-main-glue": ["Source/main.cpp"],
}
PR5_COUNT = 34 + 14   # 34 adds + 12 scaler mods + display.h + sdl_compat.h (mouse-mapping fix)
SUPERSEDED_BRANCHES = [
    "pr/2-wav-audio-robustness",     # PR #8751 closed - upstream sanitizes the audio data itself
    "pr/3-robustness-guards",        # PR #8752 closed - crash was fork-side, gated on the tip
    "pr/nonstatic-slangvk-implib",   # fix-stage, folded into pr/5
    "pr/scaler-mouse-mapping",       # fix-stage, folded into pr/5
    "fix/sdl-compat-scaler-decl",    # merged into libslang-vk (dea220af4)
]

UPSTREAM_REPO = "diasurgical/DevilutionX"
PLAN_PRS = {                      # plan branch -> upstream PR number
    BR1: 8750,
    "pr/4-test-missing-asset-guards": 8753,
    "pr/5-gpu-scaler": 8754,
    "pr/6-sdl3-main-glue": 8755,
}


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
    if br == "pr/5-gpu-scaler":
        sc = repo.git("show", f"{br}:Source/utils/sdl_compat.h", check=False)
        if not sc:
            print(f"  FAIL {br}: Source/utils/sdl_compat.h hook missing "
                  f"(pre-recut branch - run --recut)")
            ok = False
        else:
            n_qual = sc.count(b"devilution::gpu_scaler::MapWindowToGame(")
            n_raw = sc.count(b"gpu_scaler::MapWindowToGame(")
            if n_qual != 2 or n_raw != 2:
                print(f"  FAIL {br}: sdl_compat.h hook calls unqualified "
                      f"(qualified {n_qual}/2, total {n_raw}/2) - Linux CI would break")
                ok = False
    exp = EXPECTED_FILES.get(br)
    if exp is not None:
        if sorted(files) != sorted(exp):
            print(f"  FAIL {br}: file set mismatch\n    got: {sorted(files)}")
            ok = False
    elif len(files) != PR5_COUNT:
        print(f"  FAIL {br}: {len(files)} files (want {PR5_COUNT})"); ok = False
    return ok


def check_prs() -> int:
    """Report the upstream PR state for every planned branch (--check-prs).

    Automates the pr/2 + pr/3 lesson: those retirements had to be noticed by
    hand. Plain HTTPS against the GitHub API, stdlib only. Unauthenticated
    calls (60/h) cover the four plan PRs comfortably; set GH_TOKEN to raise
    the limit to 5000/h.
    """
    width = max(len(b) for b in PLAN_PRS)
    token = os.environ.get("GH_TOKEN", "")
    print("== upstream PR status ==")
    unknown = 0
    action = 0
    for br in sorted(PLAN_PRS, key=lambda b: PLAN_PRS[b]):
        n = PLAN_PRS[br]
        req = urllib.request.Request(
            f"https://api.github.com/repos/{UPSTREAM_REPO}/pulls/{n}",
            headers={"User-Agent": "build_r4_branches.py",
                     "Accept": "application/vnd.github+json"})
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                data = json.load(r)
        except urllib.error.HTTPError as e:
            unknown += 1
            hint = " - rate limited? set GH_TOKEN" if e.code in (403, 429) else ""
            print(f"WARN   {br:<{width}}  #{n}  HTTP {e.code}{hint}")
            continue
        except urllib.error.URLError as e:
            unknown += 1
            print(f"WARN   {br:<{width}}  #{n}  {e.reason}")
            continue
        state = data.get("state", "unknown")
        merged_at = data.get("merged_at")
        if merged_at:
            action += 1
            print(f"MERGED {br:<{width}}  #{n}  merged {merged_at[:10]} - rerun "
                  "--recut --push --prune, then add the branch to SUPERSEDED_BRANCHES")
        elif state == "closed":
            action += 1
            print(f"CLOSED {br:<{width}}  #{n}  closed (not merged) - retire: add "
                  "to SUPERSEDED_BRANCHES and run --prune")
        else:
            print(f"OK     {br:<{width}}  #{n}  open")
    if unknown:
        print(f"PR-CHECK: INCOMPLETE ({unknown} of {len(PLAN_PRS)} unknown)")
        return 1
    if action:
        print(f"PR-CHECK: {action} plan PR(s) need action (see above)")
        return 1
    print(f"PR-CHECK: OK - all {len(PLAN_PRS)} plan PRs open")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.getcwd())
    ap.add_argument("--worktree-root", default=None)
    ap.add_argument("--upstream", default="upstream/master")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--recut", action="store_true",
                    help="refresh flow: fetch refs, rebuild every pr/* from the current "
                         "fork tip (implies --force); run after validating the fixes")
    ap.add_argument("--push", action="store_true",
                    help="after a clean build, force-push (lease) the plan branches to origin")
    ap.add_argument("--prune", action="store_true",
                    help="after a clean build, delete retired/superseded branches (local + origin)")
    ap.add_argument("--check-prs", action="store_true",
                    help="query the upstream PR state for the plan branches, flag "
                         "closed/merged ones as retire/prune candidates, and exit")
    ap.add_argument("--verify-only", action="store_true")
    args = ap.parse_args()

    repo = Repo(os.path.abspath(args.repo))
    wt_root = args.worktree_root or os.path.join(os.path.dirname(repo.path), "r4-worktrees")

    if args.check_prs:
        return check_prs()

    if args.recut:
        args.force = True
        old_tip = repo.git("rev-parse", f"{FORK}^{{commit}}").decode().strip()
        fetches = [("origin", "libslang-vk")]
        if "/" in args.upstream:
            fetches.append((args.upstream.split("/", 1)[0], args.upstream.split("/", 1)[1]))
        for remote, ref in fetches:
            r = subprocess.run(["git", "-C", repo.path, "fetch", remote, ref],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            if r.returncode != 0:
                err = r.stderr.decode(errors="replace").strip().splitlines()
                print(f"WARN: fetch {remote} {ref} failed ({err[-1] if err else 'git error'})"
                      " - continuing on stale refs")
        new_tip = repo.git("rev-parse", f"{FORK}^{{commit}}").decode().strip()
        print(f"payload tip {FORK}: {old_tip[:9]} -> {new_tip[:9]}"
              + (" (unchanged)" if old_tip == new_tip else ""))

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
        old_tip = None
        if repo.git("branch", "--list", br, check=False).strip():
            if not args.force:
                print("  SKIP: branch exists (use --force/--recut to rebuild)")
                continue
            old_tip = repo.git("rev-parse", "--verify", f"{br}^{{commit}}").decode().strip()
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
                                 check=True, stdout=subprocess.PIPE).stdout.strip().decode()
            tree = subprocess.run(["git", "-C", wt, "rev-parse", "HEAD^{tree}"],
                                  check=True, stdout=subprocess.PIPE).stdout.strip().decode()
            keep = False
            if old_tip is not None:
                old_tree = repo.git("rev-parse", f"{old_tip}^{{tree}}").decode().strip()
                old_base = repo.git("rev-parse", f"{old_tip}^").decode().strip()
                base_commit = repo.git("rev-parse", "--verify",
                                       f"{base}^{{commit}}").decode().strip()
                keep = (old_tree == tree and old_base == base_commit)
            if keep:
                print(f"  unchanged vs {old_tip[:9]} (same tree, same base) - branch kept")
            else:
                if old_tip is not None:
                    repo.git("branch", "-D", br)
                repo.git("update-ref", f"refs/heads/{br}", tip)
                stat = repo.git("diff", "--stat", f"{base}..{br}").decode().splitlines()[-1]
                print(f"  {stat}")
            if not verify(repo, pr):
                failures += 1
        finally:
            subprocess.run(["git", "-C", repo.path, "worktree", "remove", "--force", wt],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if failures:
        print("DONE:", f"{failures} branch(es) failed - --push/--prune skipped")
        return 1
    if args.push:
        for pr in prs:
            repo.git("push", "--force-with-lease", "origin", pr["branch"])
        print("pushed:", " ".join(pr["branch"] for pr in prs))
        print("PR bases: pr/1, pr/4, pr/6 -> diasurgical:master; "
              "pr/5 -> this fork's pr/1-sdl3-build-fixes branch")
    if args.prune:
        heads = subprocess.run(["git", "-C", repo.path, "ls-remote", "--heads", "origin"],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        remote_heads = set()
        if heads.returncode == 0:
            remote_heads = {l.split(b"\t", 1)[1][len(b"refs/heads/"):].decode()
                            for l in heads.stdout.splitlines() if b"\t" in l}
        else:
            print("note: ls-remote origin failed - skipping remote branch cleanup")
        for br in SUPERSEDED_BRANCHES:
            if repo.git("branch", "--list", br, check=False).strip():
                repo.git("branch", "-D", br)
                print(f"pruned retired local branch: {br}")
            if br in remote_heads:
                d = subprocess.run(["git", "-C", repo.path, "push", "origin", "--delete", br],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if d.returncode == 0:
                    print(f"pruned retired remote branch: origin/{br}")
                else:
                    print(f"WARN: could not delete origin/{br}: "
                          + d.stderr.decode(errors="replace").strip().splitlines()[-1])
    print("DONE: all branches verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
