# tools/ci/windows.ps1
#
# Run the Windows CI preset on a real Windows host.
#
#   pwsh tools/ci/windows.ps1
#   pwsh tools/ci/windows.ps1 -Preset ci-windows-clang-cl
#
# This is a bootstrap helper, not a build system. It locates LLVM, clears
# ambient flags, removes the build tree and calls the same three commands the
# workflow calls. Build policy lives in CMakePresets.json.
#
# Prerequisites, installed once (see docs/DEVELOPER_GUIDE.md):
#
#   choco install llvm --version=22.1.7
#   choco install ninja cmake
#
# and a developer shell that has MSVC on PATH, because clang-cl uses the MSVC
# headers, libraries and ABI:
#
#   Launch "x64 Native Tools Command Prompt for VS 2022", then: pwsh

[CmdletBinding()]
param(
    [string] $Preset = 'ci-windows-clang-cl',
    [switch] $Dirty
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

# -----------------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------------

if (-not $env:LLVM_ROOT) {
    $candidate = Join-Path $env:ProgramFiles 'LLVM'

    if (Test-Path (Join-Path $candidate 'bin/clang-cl.exe')) {
        $env:LLVM_ROOT = $candidate
    }
}

if (-not $env:LLVM_ROOT -or -not (Test-Path $env:LLVM_ROOT)) {
    Write-Error @'
LLVM was not found.

  choco install llvm --version=22.1.7

Or set LLVM_ROOT to an existing installation.
'@
}

# clang-cl compiles against the MSVC toolchain, so its absence is a wrong
# build rather than a missing convenience.
if (-not $env:VCINSTALLDIR -and -not (Get-Command 'cl.exe' -ErrorAction SilentlyContinue)) {
    Write-Error @'
MSVC is not on PATH.

clang-cl uses the MSVC headers, libraries and ABI, so this preset must run
inside a developer shell:

  "x64 Native Tools Command Prompt for VS 2022", then: pwsh
'@
}

$env:CPPL_CI_PRESET = $Preset

# -----------------------------------------------------------------------------
# Ambient flags
# -----------------------------------------------------------------------------
#
# CMake folds these into the cache on the first configure, which would make
# this build differ from the runner's. cmake/ci/HostEnvironment.cmake fails the
# configure if any survive.

foreach ($variable in 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'LIBRARY_PATH', 'CPATH') {
    Remove-Item "env:$variable" -ErrorAction SilentlyContinue
}

# -----------------------------------------------------------------------------
# Run
# -----------------------------------------------------------------------------

if (-not $Dirty) {
    $binaryDir = Join-Path $root "build/ci/$($Preset -replace '^ci-', '')"

    if (Test-Path $binaryDir) {
        cmake -E rm -rf $binaryDir
    }
}

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --preset $Preset
exit $LASTEXITCODE
