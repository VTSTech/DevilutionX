<#
.SYNOPSIS
  Local Windows 11 build script for VTSTech/DevilutionX-libslang-vk (SDL3 + GPU shader scaler).

.DESCRIPTION
  Reproduces the exact recipe of the fork's CI "GPU scaler build" Windows job
  (.github/workflows/gpu-scaler-build.yml) on a local desktop, and adds the
  non-static (dynamically linked) variant that the new CI jobs build:

    -Link static    (default) Fully static SDL3/slang embed. This is the
                    CI-verified recipe: vendored SDL3 3.5.0 + libslang-vk
                    built from source, all linked INTO devilutionx.exe.
                    Output: a single self-contained exe (+ pdb).

    -Link nonstatic Dynamic build: libslang-vk ships as libslangvk.dll and the
                    SDL3 trio ships as SDL3.dll / SDL3_image.dll /
                    SDL3_mixer.dll beside the exe (GPU_SCALER_SHARED=ON,
                    DEVILUTIONX_STATIC_SDL3=OFF, DEVILUTIONX_STATIC_SDL_MIXER=OFF).
                    The script collects the DLLs into a runnable package folder
                    and smoke-tests THAT folder exactly like a user machine.

    -Link both      Builds static first, then nonstatic (two build trees,
                    so the CMake caches never fight each other).

  The script reproduces these CI steps 1:1:
    - MSVC x64 dev environment (vswhere + VsDevShell, like ilammy/msvc-dev-cmd)
    - vcpkg pinned at the manifest baseline 40f3c709db80acf154ac4b17a1f83c564ebd022e
      (like lukka/run-vcpkg) - FULL clone into <repo>\.vcpkg-local unless
      -VcpkgRoot. A full clone (what CI's run-vcpkg makes too) keeps vcpkg's
      port-version resolution offline-capable: a blobless partial clone has to
      fetch git blobs from github.com mid-install and dies when the network
      flakes ("Could not resolve host: github.com" / "could not fetch ...
      from promisor remote")
    - A standalone "vcpkg install" pre-step with the exact roots/triplet/
      features the vcpkg toolchain will use, so manifest failures surface
      with vcpkg's own clean error output instead of CMake's confusing
      knock-on errors ("vcpkg install failed" + bogus "unable to find Ninja"
      + "CMAKE_C_COMPILER not set" - those three are ONE failure: when the
      vcpkg toolchain fatal-errors inside CMakeDetermineSystem, CMake's
      make-program and compiler detection never complete)
    - CMake configure with the same -D flags the CI passes (the preset is
      expanded manually so each variant gets its own build directory)
    - Post-configure removal of the vcpkg SDL3 payload whose older headers
      shadow the FetchContent SDL 3.5.0 build (the CI "Remove vcpkg SDL3" step)
    - Build, then a --version smoke test of the packaged exe

.PARAMETER Repo
  Path to the DevilutionX-libslang-vk checkout. Default: the folder this
  script sits in, if it contains CMakeLists.txt; otherwise the current dir.

.PARAMETER Link
  static | nonstatic | both. Default: static.

.PARAMETER VcpkgRoot
  Use an existing vcpkg checkout instead of provisioning <repo>\.vcpkg-local.
  Its commit does not need to match the baseline (the manifest's
  builtin-baseline governs package versions), but matching is recommended.

.PARAMETER ResetVcpkg
  Replace the auto-provisioned <repo>\.vcpkg-local with a fresh FULL clone
  before building. downloads\ (cached port source tarballs) is preserved, so
  nothing large is re-downloaded. Run this ONCE if the tree was provisioned
  by script v1 (blobless partial clone). Refused when -VcpkgRoot is passed.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build-local-win11.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build-local-win11.ps1 -Link nonstatic
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build-local-win11.ps1 -Link both -Repo D:\src\DevilutionX-libslang-vk
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build-local-win11.ps1 -ResetVcpkg

.NOTES
  Prerequisites: Visual Studio 2022 (Community or Build Tools) with the
  "Desktop development with C++" workload, Git for Windows, and internet
  access for the first run (vcpkg clone + vendored SDL3/glslang downloads).

  Troubleshooting:
  - "Could not resolve host: github.com" during "Running vcpkg install":
    git could not reach github.com while vcpkg resolved port versions.
    Retry when the network is up (a partial clone self-heals on retry), set a
    git proxy if you use one, and run once with -ResetVcpkg for a permanently
    offline-safe full clone.
  - "feature sdl3 was passed, but that is not a feature supported by
    devilutionx": the checkout is older than 2026-10-15 - its vcpkg.json
    predates the sdl3 feature that USE_SDL3=ON asks for. git pull, or add
    the sdl3 feature block to vcpkg.json (the script's repo preflight dies
    early with the exact JSON).
  - "unable to find Ninja" + "CMAKE_C/CXX_COMPILER not set" right after a
    vcpkg error: not a tooling problem - it is CMake cascade noise from the
    vcpkg toolchain failure. Fix the vcpkg error (see the log tail the script
    prints) and the rest disappears.
  - "target_compile_features no known features for CXX compiler" at generate
    time (e.g. "MSVC version 12.0."): cmake identified a cl.exe that is NOT a
    VS2022+ MSVC - typically a DDK/VC6-era compiler that is first on PATH.
    vcpkg locates Visual Studio itself, so the vcpkg stage succeeds while
    cmake fails. Script v5 verifies the cl.exe banner, imports the VS dev
    environment over a broken PATH cl, and drops the stale CMake cache the
    bad configure left behind - re-run and it self-heals.
  - "add_subdirectory ... <repo>\dist does not contain a CMakeLists.txt" and
    "install FILES given directory \"\" to install" at configure time: a
    repo-root dist\ folder exists (script v4 put its package output there!).
    devilutionX's CMakeLists.txt (line 16) treats ANY dist\ folder as a
    release source-distribution payload: IS_DIRECTORY dist -> set(SRC_DIST
    ON) + add_subdirectory(dist) -> ${DEVILUTIONX_MPQ} stays empty and the
    install rules die. Script v5 packages into dist-local\ and migrates a
    stray dist\ there automatically at startup.
#>
param(
    [string]$Repo = "",
    [ValidateSet('static', 'nonstatic', 'both')]
    [string]$Link = 'static',
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [switch]$ResetVcpkg
)

$ErrorActionPreference = 'Stop'

$VcpkgCommit = '40f3c709db80acf154ac4b17a1f83c564ebd022e'  # vcpkg.json builtin-baseline, same commit the CI's run-vcpkg pins

# ----------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------
function Write-Step([string]$msg) { Write-Host "`n=== $msg" -ForegroundColor Cyan }
function Write-Info([string]$msg) { Write-Host "    $msg" }
function Write-Warn2([string]$msg) { Write-Host "    WARNING: $msg" -ForegroundColor Yellow }
function Die([string]$msg) {
    Write-Host "`nERROR: $msg" -ForegroundColor Red
    exit 1
}

# First file matching -Name under the given ABSOLUTE directory trees, skipping
# vcpkg_installed (its SDL3 payload is deleted post-configure anyway).
function Get-BuiltFile([string]$name, [string[]]$searchRoots) {
    foreach ($dir in $searchRoots) {
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        $hit = Get-ChildItem -LiteralPath $dir -Recurse -Filter $name -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch 'vcpkg_installed|\\pkg' } |
            Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

# Blobless partial clones (script v1 provisioned one) must fetch git blobs
# from github.com on demand whenever vcpkg's versioning checks out a
# historical port tree (e.g. the lua@5.4.6 override). A full clone - what the
# CI's lukka/run-vcpkg makes - carries all history locally and never does.
function Warn-IfPartialClone([string]$root) {
    if (-not (Test-Path (Join-Path $root '.git'))) { return }
    $filter = git -C $root config --get remote.origin.partialclonefilter
    if ($LASTEXITCODE -eq 0 -and $filter) {
        Write-Warn2 "vcpkg at $root is a blobless PARTIAL clone (filter '$filter')"
        Write-Warn2 "vcpkg version resolution will fetch git blobs from github.com on demand"
        Write-Warn2 "retrying works once the network is back, but re-run with -ResetVcpkg once for an offline-safe FULL clone"
    }
}

# Called when cmake configure fails: print the vcpkg manifest-install log tail
# plus targeted hints for the known failure signatures, so the real cause is
# visible without hunting through CMake's knock-on error cascade.
function Show-VcpkgFailureDiagnostics([string]$buildDir) {
    $log = Join-Path $buildDir 'vcpkg-manifest-install.log'
    if (-not (Test-Path -LiteralPath $log)) { return }
    Write-Host ""
    Write-Warn2 "vcpkg manifest install log tail ($log):"
    Write-Host "    ------------------------------------------------------------"
    Get-Content -LiteralPath $log -Tail 40 | ForEach-Object { Write-Host "    $_" }
    Write-Host "    ------------------------------------------------------------"
    $text = Get-Content -LiteralPath $log -Raw
    if ($text -match 'Could not resolve host|exit code 128|promisor remote') {
        Write-Host "    HINT: git could not reach github.com while vcpkg resolved port versions."
        Write-Host "    Check internet/DNS, retry, or set a proxy:"
        Write-Host "      git config --global http.proxy http://<host>:<port>"
        Write-Host "    Re-run with -ResetVcpkg once for a full clone that needs no such fetches."
    }
    if ($text -match 'not a feature supported by') {
        Write-Host "    HINT: the checkout's vcpkg.json lacks the sdl3 feature (tree predates"
        Write-Host "    2026-10-15). Update the checkout (git pull) or add the sdl3 feature."
    }
    if ($text -match 'Access is denied|Permission denied') {
        Write-Host "    HINT: antivirus/ransomware protection may be blocking vcpkg - exclude"
        Write-Host "    $env:VCPKG_ROOT and the build dir from real-time scanning."
    }
    if ($text -match 'The system cannot find the path specified') {
        Write-Host "    HINT: path-length limit. Enable Win32 long paths"
        Write-Host "    (HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled=1)"
        Write-Host "    or move the checkout to a shorter folder."
    }
    if ($text -match 'Could not find Visual Studio') {
        Write-Host "    HINT: vcpkg could not detect MSVC - install VS2022 'Desktop development with C++'."
    }
}

# ----------------------------------------------------------------------------
# 1. resolve the repo
# ----------------------------------------------------------------------------
Write-Step "DevilutionX-libslang-vk local build (Link=$Link)"

if (-not $Repo -or $Repo -eq "") {
    $self = $PSScriptRoot
    if ($self -and (Test-Path (Join-Path $self 'CMakeLists.txt'))) { $Repo = $self }
    else { $Repo = (Get-Location).Path }
}
$Repo = (Resolve-Path -LiteralPath $Repo).Path
if (-not (Test-Path (Join-Path $Repo 'CMakeLists.txt'))) {
    Die "No CMakeLists.txt under '$Repo' - pass -Repo <path to the DevilutionX-libslang-vk checkout>"
}
if (-not (Test-Path (Join-Path $Repo 'Packaging\windows\CMakePresets.json'))) {
    Die "'$Repo' does not look like a DevilutionX checkout (missing Packaging\windows\CMakePresets.json)"
}
Write-Info "repo:      $Repo"

# Tree-vintage preflight: this recipe needs the manifest's sdl3 feature. The
# USE_SDL3 option landed 2025-10-04 (c0975bfa8) - from then on the vcpkg
# toolchain passes --x-feature=sdl3 - but vcpkg.json only GAINED the feature
# on 2026-10-15 (caee40ef3). A checkout in that window makes vcpkg warn
# "feature sdl3 was passed, but that is not a feature supported by
# devilutionx" and silently skip SDL3/sdl3-image.
$vcpkgManifest = Join-Path $Repo 'vcpkg.json'
if (-not (Test-Path -LiteralPath $vcpkgManifest)) { Die "missing $vcpkgManifest" }
if (-not (Select-String -LiteralPath $vcpkgManifest -Pattern '"sdl3"' -Quiet)) {
    $msg = @"
the checkout's vcpkg.json has no 'sdl3' feature, but this recipe passes
--x-feature=sdl3 (USE_SDL3=ON): vcpkg silently skips SDL3/sdl3-image and the
build later dies on missing SDL3 headers. Your checkout predates 2026-10-15.

Fix (either):
  git pull                      # update to a commit at/after caee40ef3
or hand-edit vcpkg.json, adding this block inside "features":
  "sdl3": {
      "description": "Use SDL3",
      "dependencies": [ "sdl3", "sdl3-image" ]
  },
"@
    Die $msg
}
if (-not (Select-String -LiteralPath (Join-Path $Repo 'CMake\VcPkgManifestFeatures.cmake') -Pattern 'USE_SDL3' -Quiet)) {
    Die "CMake/VcPkgManifestFeatures.cmake has no USE_SDL3 branch - the checkout is too old for this recipe; update it (git pull)"
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Die "git not found on PATH - install Git for Windows (https://git-scm.com/download/win)"
}

# Make Git for Windows' usr\bin visible so CMake's find_package(Patch) finds
# patch.exe: 3rdParty/SDL3_mixer applies the WAV-decoder patch through it.
# Without it the mixer builds UNPATCHED and 1996-era WAVs (Diablo SFX) fail
# to decode at runtime. Harmless no-op when patch.exe is already findable.
$gitExe = (Get-Command git).Source
$gitUsrBin = Join-Path (Split-Path (Split-Path $gitExe -Parent) -Parent) 'usr\bin'
if (Test-Path (Join-Path $gitUsrBin 'patch.exe')) {
    $env:PATH = "$gitUsrBin;$env:PATH"
    Write-Info "patch.exe: $gitUsrBin (SDL3_mixer WAV-decoder patch will apply)"
} else {
    Write-Warn2 "patch.exe not found next to git - SDL3_mixer may build without the zero-length-RIFF WAV fix"
}

# ----------------------------------------------------------------------------
# 1b. Package output folder: <repo>\dist-local (NEVER <repo>\dist)
# ----------------------------------------------------------------------------
# devilutionX's CMakeLists.txt treats a repo-root dist\ folder as a release
# source-distribution payload:
#   line 16: if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
#     -> set(SRC_DIST ON) + add_subdirectory(dist)
# With the script's package output in dist\ (v4 behaviour) configure dies:
#   "add_subdirectory ... dist does not contain a CMakeLists.txt" and
#   "install FILES given directory \"\" to install" (${DEVILUTIONX_MPQ} empty).
# So packages live in dist-local\ and a stray dist\ is migrated there once.
$DistLocal = Join-Path $Repo 'dist-local'
$RepoDist = Join-Path $Repo 'dist'
if (Test-Path -LiteralPath $RepoDist -PathType Container) {
    if (Test-Path -LiteralPath $DistLocal) {
        Die "both dist\ and dist-local\ exist in the repo - move the contents of dist\ into dist-local\ (or delete dist\) and re-run: devilutionX's cmake misdetects a repo-root dist\ folder as a release source distribution (CMakeLists.txt:16 'IS_DIRECTORY dist' -> add_subdirectory(dist) -> configure fails)."
    }
    try {
        Rename-Item -LiteralPath $RepoDist -NewName 'dist-local' -ErrorAction Stop
        Write-Info "migrated <repo>\dist -> <repo>\dist-local (a repo-root dist\ makes cmake think this is a release source distribution)"
    } catch {
        Die "could not rename $RepoDist to dist-local ($($_.Exception.Message)).`n    Close anything using dist\ (a running devilutionx.exe, an Explorer window inside it) and re-run."
    }
}
# Keep dist-local\ out of git status WITHOUT touching the tracked .gitignore:
# append to the local-only exclude file (.git/info/exclude - never committed).
$gitInfo = Join-Path $Repo '.git\info'
$gitExclude = Join-Path $gitInfo 'exclude'
if (Test-Path -LiteralPath (Join-Path $Repo '.git')) {
    if (-not ((Test-Path -LiteralPath $gitExclude) -and ((Get-Content -LiteralPath $gitExclude -ErrorAction SilentlyContinue) -contains '/dist-local/'))) {
        try {
            New-Item -ItemType Directory -Force -Path $gitInfo | Out-Null
            Add-Content -LiteralPath $gitExclude -Value "`n# local build packages (build-local-win11.ps1 v5)`n/dist-local/" -ErrorAction Stop
            Write-Info "git:       /dist-local/ added to .git/info/exclude (local-only, never committed)"
        } catch {
            Write-Warn2 "could not update .git/info/exclude - dist-local\ may show in git status (harmless)"
        }
    }
}

# ----------------------------------------------------------------------------
# 2. MSVC x64 dev environment (same effect as the CI's ilammy/msvc-dev-cmd)
# ----------------------------------------------------------------------------
Write-Step "MSVC x64 environment"

$vs = $null

# ---------------------------------------------------------------------------
# Compiler sanity: CMake's Ninja generator identifies whatever cl.exe is FIRST
# on PATH. A DDK/VC6-era cl (banner "Compiler Version 12.00...") identifies as
# "MSVC 12.0", for which CMake records NO C++ features, and every cxx_std_23
# target (libslang-vk's slangvk_add_library) then dies at generate time with
# "target_compile_features no known features for CXX compiler". vcpkg is
# immune - it locates Visual Studio itself - which is exactly why the vcpkg
# stage can pass while cmake fails. So: verify the cl.exe banner and the dev
# environment, never reuse PATH blindly; on mismatch import VS, which
# PREPENDS the real toolchain to PATH and shadows the broken cl.
# ---------------------------------------------------------------------------
function Get-ClInfo {
    # cl.exe prints its banner on stderr; merge inside cmd so PowerShell sees
    # plain strings (PS 5.1 would otherwise wrap native stderr in ErrorRecords).
    $lines = cmd /c "cl 2>&1"
    $joined = $lines -join "`n"
    # Parse via [regex]::Match into explicit group objects - do NOT read
    # $Matches after the pipeline below. Where-Object scriptblocks run in the
    # CALLER's scope in Windows PowerShell (why "$sum += $_" works), so the
    # groupless -match there REPLACES $Matches (keys 1/2 gone) and v3 parsed
    # EVERY cl.exe as "MSVC 0.0" - the real VS2022/VS2026 compiler then failed
    # the 19.30 check even though its banner was fine.
    $m = [regex]::Match($joined, 'Compiler Version\s+(\d+)\.(\d+)')
    if ($m.Success) {
        $banner = $lines | Where-Object { $_ -and $_.Contains('Compiler Version') } | Select-Object -First 1
        return [pscustomobject]@{ Major = [int]$m.Groups[1].Value; Minor = [int]$m.Groups[2].Value; Banner = $banner }
    }
    return $null
}
function Test-ClVersionSupported($cl) {
    # devilutionX + libslang-vk build with C++20/23: VS2022 (MSVC 19.30) or
    # newer - VS2026's 19.5x toolsets (e.g. 19.51) pass this check too.
    return ($null -ne $cl -and ($cl.Major -gt 19 -or ($cl.Major -eq 19 -and $cl.Minor -ge 30)))
}

$cl = $null
if (Get-Command cl -ErrorAction SilentlyContinue) {
    $cl = Get-ClInfo
    if ((Test-ClVersionSupported $cl) -and $env:INCLUDE) {
        Write-Info "cl.exe on PATH is a supported MSVC and the dev env is initialized - reusing it"
        # Best effort: still locate the VS install so the bundled cmake/ninja
        # fallback below can kick in if no standalone copies exist.
        $vswhere0 = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere0) {
            $vs = & $vswhere0 -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        }
    } else {
        if ($cl -and -not $env:INCLUDE) {
            Write-Warn2 "cl.exe on PATH is a supported MSVC but INCLUDE is empty - the developer"
            Write-Warn2 "  environment is not initialized in this prompt. Importing VS instead."
        } elseif ($cl) {
            Write-Warn2 "cl.exe FIRST ON PATH is NOT a supported MSVC - ignoring it:"
            Write-Warn2 "  path:   $((Get-Command cl).Source)"
            Write-Warn2 "  banner: $($cl.Banner)"
            Write-Warn2 "  CMake would identify it as MSVC $($cl.Major).$($cl.Minor): an empty C++ feature"
            Write-Warn2 "  table -> 'target_compile_features no known features for CXX compiler'."
        } else {
            Write-Warn2 "cl.exe FIRST ON PATH has no MSVC banner - not a real MSVC. Importing VS instead:"
            Write-Warn2 "  path:   $((Get-Command cl).Source)"
        }
        Write-Warn2 "  (vcpkg finds Visual Studio by itself, which is why the vcpkg stage passed)"
        $cl = $null
        # fall through: the VS import below PREPENDS the real toolchain to PATH,
        # shadowing whatever cl.exe the ambient PATH exposed
    }
} else {
    Write-Info "no cl.exe on PATH - importing the VS developer environment"
}

if (-not $cl) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        Die "vswhere not found and no supported cl.exe is available. Install Visual Studio 2022 or 2026 (Community or Build Tools) with the 'Desktop development with C++' workload."
    }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) {
        Die "No Visual Studio with the C++ workload (Microsoft.VisualStudio.Component.VC.Tools.x86.x64) found. Install VS 2022 / VS 2026 Community or Build Tools with 'Desktop development with C++'."
    }
    Write-Info "VS:        $vs"
    $devShell = Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path -LiteralPath $devShell)) { Die "DevShell module missing at $devShell - VS install looks incomplete" }
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) { Die "cl.exe is still not on PATH after Enter-VsDevShell - VS install looks incomplete" }
    $cl = Get-ClInfo
    if (-not (Test-ClVersionSupported $cl)) {
        $bannerLine = if ($cl) { $cl.Banner } else { '<no MSVC banner>' }
        Die "cl.exe after Enter-VsDevShell is not a supported MSVC (need MSVC 19.30+ - VS2022 19.3x/19.4x or VS2026 19.5x).`n    path:   $((Get-Command cl).Source)`n    banner: $bannerLine"
    }
}
Write-Info "cl.exe:    $((Get-Command cl).Source)"
Write-Info "cl:        $($cl.Banner)"

# ----------------------------------------------------------------------------
# 3. cmake + ninja (standalone preferred, VS-bundled as fallback)
# ----------------------------------------------------------------------------
Write-Step "CMake + Ninja"

$vsCMakeBin = if ($vs) { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin' } else { $null }
$vsNinjaBin = if ($vs) { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja' } else { $null }

if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -and $vsCMakeBin -and (Test-Path (Join-Path $vsCMakeBin 'cmake.exe'))) {
    $env:PATH = "$vsCMakeBin;$env:PATH"
    Write-Info "using VS-bundled CMake"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Die "cmake not found. Install it: winget install Kitware.CMake  (or let VS's bundled copy be detected - re-run from an empty prompt)"
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue) -and $vsNinjaBin -and (Test-Path (Join-Path $vsNinjaBin 'ninja.exe'))) {
    $env:PATH = "$vsNinjaBin;$env:PATH"
    Write-Info "using VS-bundled Ninja"
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Die "ninja not found. Install it: winget install Ninja-build.Ninja  (VS's bundled ninja is used automatically when standalone ninja is absent)"
}
Write-Info "cmake:     $((Get-Command cmake).Source)"
Write-Info "ninja:     $((Get-Command ninja).Source)"
$cmakeVer = (& cmake --version | Select-Object -First 1)
Write-Info "cmake ver: $cmakeVer"
$mv = [regex]::Match("$cmakeVer", 'cmake version (\d+)\.(\d+)')
if ($mv.Success) {
    $cmj = [int]$mv.Groups[1].Value; $cmn = [int]$mv.Groups[2].Value
    if (($cmj -lt 3) -or (($cmj -eq 3) -and ($cmn -lt 21))) {
        Write-Warn2 "cmake $cmj.$cmn is old - CMake >= 3.21 is needed to record cxx_std_23 for MSVC;"
        Write-Warn2 "  if configure dies on 'cxx_std_23 not known', use the CMake bundled with Visual Studio."
    }
}

# ----------------------------------------------------------------------------
# 4. vcpkg at the manifest baseline (like the CI's lukka/run-vcpkg step)
# ----------------------------------------------------------------------------
Write-Step "vcpkg"

if ($VcpkgRoot -and $ResetVcpkg) {
    Die "-ResetVcpkg only applies to the auto-provisioned <repo>\.vcpkg-local - refusing to wipe an explicit -VcpkgRoot"
}

if ($VcpkgRoot) {
    if (-not (Test-Path -LiteralPath $VcpkgRoot)) {
        Die "-VcpkgRoot '$VcpkgRoot' does not exist - pass the checkout dir or leave it unset to auto-provision"
    }
    Write-Info "using existing VCPKG_ROOT: $VcpkgRoot"
    $head = git -C $VcpkgRoot rev-parse HEAD
    if ($head -and ($head -ne $VcpkgCommit)) {
        Write-Warn2 "vcpkg HEAD $head != manifest baseline $VcpkgCommit (fine: vcpkg.json's builtin-baseline still pins package versions)"
    }
    Warn-IfPartialClone $VcpkgRoot
} else {
    $VcpkgRoot = Join-Path $Repo '.vcpkg-local'
    $oldTree = $null

    if ($ResetVcpkg -and (Test-Path -LiteralPath $VcpkgRoot)) {
        Write-Info "-ResetVcpkg: replacing $VcpkgRoot with a fresh FULL clone (downloads\ preserved) ..."
        $oldTree = Join-Path $Repo (".vcpkg-local-old-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
        Move-Item -LiteralPath $VcpkgRoot -Destination $oldTree  # instant rename; immune to locks, read-only files, long paths
    }

    if (Test-Path (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat')) {
        Write-Info "reusing provisioned vcpkg: $VcpkgRoot"
        Warn-IfPartialClone $VcpkgRoot
    } else {
        # FULL clone on purpose: vcpkg's versioning checks out HISTORICAL port
        # trees (the lua@5.4.6 override resolves an old commit). A blobless
        # clone fetches those blobs from github.com mid-install; a full clone
        # keeps them local, exactly like the CI's lukka/run-vcpkg checkout.
        Write-Info "cloning microsoft/vcpkg into $VcpkgRoot (FULL clone - version resolution works offline) ..."
        git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
        if ($LASTEXITCODE -ne 0) {
            Write-Host "    HINT: git could not reach github.com - check internet/DNS, retry, or set a proxy:"
            Write-Host "      git config --global http.proxy http://<host>:<port>"
            Die "git clone of vcpkg failed"
        }
        if ($oldTree -and (Test-Path (Join-Path $oldTree 'downloads'))) {
            Move-Item -LiteralPath (Join-Path $oldTree 'downloads') -Destination (Join-Path $VcpkgRoot 'downloads')
            Write-Info "preserved cached port downloads from the previous tree"
        }
    }

    # dispose of the replaced tree: robocopy /MIR deletes what Remove-Item
    # chokes on (read-only git objects, >260-char buildtrees paths)
    if ($oldTree -and (Test-Path -LiteralPath $oldTree)) {
        $empty = Join-Path $env:TEMP ("vcpkg-empty-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $empty -Force | Out-Null
        robocopy $empty $oldTree /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -band 8) {
            Write-Warn2 "could not fully delete $oldTree (robocopy exit $LASTEXITCODE) - harmless, delete by hand later"
        }
        Remove-Item -LiteralPath $empty -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $oldTree -Recurse -Force -ErrorAction SilentlyContinue
    }

    git -C $VcpkgRoot cat-file -e "$VcpkgCommit^{commit}"
    if ($LASTEXITCODE -ne 0) {
        Write-Info "fetching pinned commit $VcpkgCommit ..."
        git -C $VcpkgRoot fetch origin $VcpkgCommit
        if ($LASTEXITCODE -ne 0) { Die "could not fetch vcpkg commit $VcpkgCommit" }
    }
    git -C $VcpkgRoot checkout $VcpkgCommit --force --detach | Out-Null
    if ($LASTEXITCODE -ne 0) { Die "checkout of vcpkg commit $VcpkgCommit failed" }
    Write-Info "vcpkg:     $VcpkgRoot @ $VcpkgCommit"
    if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
        Write-Info "bootstrapping vcpkg ..."
        & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) { Die "bootstrap-vcpkg.bat failed" }
    }
}
$env:VCPKG_ROOT = $VcpkgRoot

# The CI copies Packaging/windows/CMakePresets.json to the repo root; this
# script expands that preset's cacheVariables manually (toolchain file,
# RelWithDebInfo, DISABLE_LTO) so each variant gets its own build dir.
Copy-Item (Join-Path $Repo 'Packaging\windows\CMakePresets.json') (Join-Path $Repo 'CMakePresets.json') -Force

# ----------------------------------------------------------------------------
# 5. build loop
# ----------------------------------------------------------------------------
function Build-Variant([string]$variant) {
    Write-Step "Configure ($variant)"

    $buildDir = Join-Path $Repo "build-local-win-$variant"
    $dist = Join-Path $DistLocal "windows-x64-$variant"

    # ---- standalone manifest install BEFORE cmake: same roots/triplet/features
    # the vcpkg toolchain will use, so a failure surfaces here with vcpkg's own
    # clean error output (the toolchain's later install becomes a cached no-op).
    # Without this, a vcpkg failure aborts cmake mid-system-detection and shows
    # the misleading "vcpkg install failed" + "unable to find Ninja" +
    # "CMAKE_C_COMPILER not set" cascade.
    Write-Step "vcpkg install ($variant)"
    $vcpkgExe = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
    $vcpkgArgs = @(
        'install',
        '--triplet', 'x64-windows',
        "--x-manifest-root=$Repo",
        "--x-install-root=$(Join-Path $buildDir 'vcpkg_installed')",
        '--x-feature=sdl3',      # USE_SDL3=ON -> VcPkgManifestFeatures.cmake appends sdl3
        '--x-feature=encryption' # PACKET_ENCRYPTION defaults ON (unless NONET)
    )
    Write-Info "vcpkg $($vcpkgArgs -join ' ')"
    & $vcpkgExe @vcpkgArgs
    if ($LASTEXITCODE -ne 0) { Die "vcpkg install failed ($variant) - fix the vcpkg error above and re-run" }

    # ---- stale compiler-state guard ----------------------------------------
    # A configure that identified a WRONG cl.exe (e.g. a DDK/VC6-era compiler
    # first on PATH -> "MSVC version 12.0", empty C++ feature table, fatal
    # "target_compile_features no known features" at generate time) bakes that
    # compiler into CMakeCache.txt + CMakeFiles\CMakeCXXCompiler.cmake, and
    # CMake keeps trusting it on every re-run even after PATH is fixed.
    # Detect such a cache and drop ONLY the CMake-side state: vcpkg_installed\
    # keeps the already-built packages, so the re-configure stays fast.
    $cache = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $cachedCl = $null
        foreach ($line in (Get-Content -LiteralPath $cache)) {
            if ($line -match '^CMAKE_CXX_COMPILER:FILEPATH=(.+)$') { $cachedCl = $Matches[1]; break }
        }
        $cachedVer = $null
        $ccFile = Get-ChildItem (Join-Path $buildDir 'CMakeFiles') -Filter 'CMakeCXXCompiler.cmake' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($ccFile) {
            foreach ($line in (Get-Content -LiteralPath $ccFile.FullName)) {
                if ($line -match 'set\(CMAKE_CXX_COMPILER_VERSION\s+"([^"]+)"\)') { $cachedVer = $Matches[1]; break }
            }
        }
        $curCl = (Get-Command cl -ErrorAction SilentlyContinue).Source
        $staleWhy = $null
        # CMake caches the compiler path with forward slashes
        # (C:/.../Hostx64/x64/cl.exe) while Get-Command reports the canonical
        # backslash form (C:\...\HostX64\x64\cl.exe). PS string -ne is already
        # case-insensitive, but the slash direction is not - normalize both,
        # else the guard false-fires and wastes a full reconfigure.
        $cachedClN = if ($cachedCl) { $cachedCl.Trim().Replace('/', '\') } else { $null }
        $curClN = if ($curCl) { $curCl.Trim().Replace('/', '\') } else { $null }
        if ($cachedClN -and $curClN -and ($cachedClN -ne $curClN)) {
            $staleWhy = "cached CMAKE_CXX_COMPILER is $cachedCl, now using $curCl"
        } elseif ($cachedVer) {
            $vp = $cachedVer.Split('.')
            if (([int]$vp[0] -lt 19) -or (([int]$vp[0] -eq 19) -and ([int]$vp[1] -lt 30))) {
                $staleWhy = "cached compiler version $cachedVer is not a supported MSVC (need >= 19.30)"
            }
        }
        if ($staleWhy) {
            Write-Warn2 "stale CMake state in $buildDir - $staleWhy"
            Write-Info "dropping CMakeCache.txt + CMakeFiles\ (vcpkg_installed\ is kept)"
            Remove-Item -LiteralPath $cache -Force
            Remove-Item (Join-Path $buildDir 'CMakeFiles') -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    # Same flags as the CI's Configure step (the preset ninja-vcpkg-relwithdebinfo
    # expanded: toolchain from VCPKG_ROOT, RelWithDebInfo, DISABLE_LTO=ON) plus
    # the per-variant linkage flags. BUILD_TESTING=OFF exactly like the CI
    # Windows job (no gtest, no ~50 test executables, no ctest). The triplet is
    # passed explicitly but equals the toolchain's default (x64-windows), same
    # as CI.
    $flags = @(
        '-S', $Repo, '-B', $buildDir, '-G', 'Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake",
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        '-DVCPKG_TARGET_TRIPLET=x64-windows',
        '-DDISABLE_LTO=ON',
        '-DUSE_SDL3=ON',
        '-DUSE_GPU_SCALER=ON',
        '-DDEVILUTIONX_SYSTEM_SDL3=OFF',
        '-DDEVILUTIONX_SYSTEM_SDL_IMAGE=OFF',
        '-DDEVILUTIONX_SYSTEM_LIBSODIUM=OFF',
        '-DDEVILUTIONX_STATIC_LIBSODIUM=ON',
        '-DBUILD_TESTING=OFF'
    )
    if ($variant -eq 'static') {
        $flags += '-DDEVILUTIONX_STATIC_SDL3=ON'
    } else {
        $flags += '-DGPU_SCALER_SHARED=ON'
        $flags += '-DDEVILUTIONX_STATIC_SDL3=OFF'
        $flags += '-DDEVILUTIONX_STATIC_SDL_MIXER=OFF'
        # The SDL3_image glue (3rdParty/SDL3_image/CMakeLists.txt) keys on
        # DEVILUTIONX_STATIC_SDL_IMAGE, NOT on DEVILUTIONX_STATIC_SDL3, and
        # dependency_options defaults it to STATIC when the lib comes from
        # source - without this flag the nonstatic build has no SDL3_image.dll
        # to ship (the CI nonstatic job's import-table gate expects the DLL).
        $flags += '-DDEVILUTIONX_STATIC_SDL_IMAGE=OFF'
        # Repo-state preflight: the MSVC nonstatic build needs the slangvk
        # import-lib fix on the checkout (0002-nonstatic-slangvk-implib-fix.patch).
        # Without it, ninja aborts with "multiple rules generate
        # _deps/libslang-vk-build/slangvk.lib" - the static libslang-vk archive
        # and the shared variant's import lib are one and the same file.
        if (-not ((Get-Content -LiteralPath (Join-Path $Repo 'CMakeLists.txt') -Raw).Contains('slangvk-shared-implib'))) {
            Write-Warn2 "this checkout is missing the slangvk-shared-implib fix (0002 patch) -"
            Write-Warn2 "  the nonstatic build is expected to die with 'multiple rules generate"
            Write-Warn2 "  _deps/libslang-vk-build/slangvk.lib'. Apply the 0002 patch to the repo first:"
            Write-Warn2 "    git am --keep-cr <download-dir>\0002-nonstatic-slangvk-implib-fix.patch"
        }
    }
    Write-Info "cmake $($flags -join ' ')"
    & cmake @flags
    if ($LASTEXITCODE -ne 0) {
        Show-VcpkgFailureDiagnostics $buildDir
        Die "configure failed ($variant)"
    }

    # ---- CI step: Remove vcpkg SDL3 (older headers shadow FetchContent SDL 3.5.0)
    # The manifest's sdl3 feature (enabled by CMake/VcPkgManifestFeatures.cmake
    # under USE_SDL3) installs SDL3 headers+libs into vcpkg_installed;
    # Dependencies.cmake's directory-scope include dirs put vcpkg's include root
    # FIRST on every target's path, so the FetchContent SDL 3.5.0 subbuild picks
    # up vcpkg's older SDL3/SDL.h and dies on version static-asserts. Nothing
    # links vcpkg's SDL3 (DEVILUTIONX_SYSTEM_SDL3=OFF) - delete its payload.
    Write-Step "Remove vcpkg SDL3 payload ($variant)"
    $root = Join-Path $buildDir 'vcpkg_installed\x64-windows'
    if (-not (Test-Path -LiteralPath $root)) { Die "missing $root - vcpkg manifest install did not run? Review before continuing" }
    $n = 0
    foreach ($sub in @('include', 'debug/include', 'share', 'debug/share', 'lib', 'debug/lib', 'bin', 'debug/bin')) {
        $dir = Join-Path $root $sub
        if (Test-Path -LiteralPath $dir) {
            Get-ChildItem -LiteralPath $dir -Filter 'SDL3*' -ErrorAction SilentlyContinue | ForEach-Object {
                Remove-Item -LiteralPath $_.FullName -Recurse -Force
                $n++
            }
        }
    }
    if ($n -eq 0) {
        # Benign when the configure above already succeeded: nothing links
        # vcpkg's SDL3 (DEVILUTIONX_SYSTEM_SDL3=OFF), so with no payload there
        # is nothing left that could shadow the FetchContent SDL 3.5.0 headers.
        Write-Warn2 "no vcpkg SDL3 payload found under $root - nothing to remove."
        Write-Warn2 "  Safe: the configure succeeded and nothing links vcpkg's SDL3, so no older"
        Write-Warn2 "  headers can shadow the FetchContent SDL 3.5.0 subbuild. Only if a build"
        Write-Warn2 "  later dies on SDL3 version static-asserts, re-check the manifest's sdl3"
        Write-Warn2 "  feature wiring (vcpkg.json + CMake/VcPkgManifestFeatures.cmake)."
    } else {
        Write-Info "removed $n vcpkg SDL3 payloads; FetchContent SDL 3.5.0 headers are now authoritative"
    }

    Write-Step "Build ($variant)"
    & cmake --build $buildDir
    if ($LASTEXITCODE -ne 0) { Die "build failed ($variant)" }

    $exe = Join-Path $buildDir 'devilutionx.exe'
    if (-not (Test-Path -LiteralPath $exe)) { Die "devilutionx.exe not produced at $exe" }

    # ---- package + smoke test the packaged exe
    Write-Step "Package ($variant)"
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    Copy-Item $exe $dist -Force
    $pdb = Join-Path $buildDir 'devilutionx.pdb'
    if (Test-Path -LiteralPath $pdb) { Copy-Item $pdb $dist -Force }
    # vcpkg's applocal post-build step (VCPKG_APPLOCAL_DEPS, on by default) copies
    # the dynamic vcpkg deps (lua, bzip2, ...) beside the exe in the build dir
    # root - ship them too, or the packaged exe cannot load.
    Get-ChildItem -LiteralPath $buildDir -Filter '*.dll' -File -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName $dist -Force
        Write-Info "shipped $($_.Name) <- build dir root"
    }
    # shader helper scripts ship beside the exe (shader corpus download / cache wipe)
    foreach ($helper in @('install_shaders.cmd', 'clear_shader_cache.cmd', 'clear_gpu-scaler.log.cmd', 'install_git.cmd')) {
        $src = Join-Path $Repo "Packaging\windows\$helper"
        if (Test-Path -LiteralPath $src) { Copy-Item $src $dist -Force }
    }

    if ($variant -eq 'nonstatic') {
        # Dynamic build: collect the DLLs the exe imports beside it
        # (Windows searches the exe's own folder first, so the package folder is
        # self-contained exactly like the CI zip). libslang-vk ships as
        # libslangvk.dll (GPU_SCALER_SHARED=ON) and the SDL3 trio as shared
        # DLLs (DEVILUTIONX_STATIC_SDL3=OFF; SDL3_image follows SDL3 in the
        # glue; SDL3_mixer via its own flag). These are NOT at the build dir
        # root (no applocal coverage for FetchContent targets) - find them in
        # the _deps fetch trees.
        $deps = Join-Path $buildDir '_deps'
        foreach ($dll in @('libslangvk.dll', 'SDL3.dll', 'SDL3_image.dll', 'SDL3_mixer.dll')) {
            $src = Get-BuiltFile $dll @((Join-Path $buildDir '_deps'), $buildDir)
            if (-not $src) {
                if ($dll -eq 'SDL3_image.dll') {
                    # Tolerate a statically linked SDL3_image (the glue's
                    # dependency_options default): the exe links it directly
                    # and no DLL needs shipping. The packaged --version smoke
                    # test below is the real gate - a missing-but-needed DLL
                    # fails there with a load error.
                    Write-Warn2 "$dll not found under the build tree - SDL3_image was linked statically, nothing to ship"
                    continue
                }
                Die "$dll not found under the build tree - the shared variant did not build (check the configure flags for $variant)"
            }
            Copy-Item $src $dist -Force
            Write-Info "shipped $dll <- $src"
        }
    }

    Write-Step "Smoke test ($variant)"
    & (Join-Path $dist 'devilutionx.exe') --version
    if ($LASTEXITCODE -ne 0) { Die "devilutionx.exe --version failed ($variant) - the packaged exe could not load its dependencies" }

    Write-Host "`n    $variant build OK"
    Write-Host "    exe:      $exe"
    Write-Host "    package:  $dist"
    return $dist
}

Push-Location $Repo
try {
    $variants = if ($Link -eq 'both') { @('static', 'nonstatic') } else { @($Link) }
    foreach ($v in $variants) { Build-Variant $v | Out-Null }

    Write-Step "All done"
    Write-Host @"

    To play, drop your MPQs into one of the dist-local\windows-x64-* package folders:
      diabdat.mpq      from your Diablo (GoG) install - required
      hellfire.mpq ... from your Hellfire install - optional expansion
      devilutionx.mpq  the assets pack from the DevilutionX releases page
    Then run dist-local\windows-x64-<variant>\devilutionx.exe.

    Vulkan: the GPU shader scaler needs a Vulkan loader at runtime
    (vulkan-1.dll - shipped with any GPU driver). Shader presets go into the
    <package>\.ugl\shaders folder via install_shaders.cmd or by hand.
"@
} finally {
    Pop-Location
}
