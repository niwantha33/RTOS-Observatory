# SPDX-License-Identifier: Apache-2.0
<#
.SYNOPSIS
  Build and test RTOS Observatory on Windows with clang-cl, MSVC libraries, Windows SDK and Ninja.
.DESCRIPTION
  Discovers a complete x64 MSVC/SDK installation without invoking VsDevCmd.bat.
  Sets process-local variables only. No installs, registry changes or source edits.
  Exits nonzero on configure, build or test failure; writes a transcript to the build folder.
.EXAMPLE
  .\tools\build-windows-clang.ps1 -Configuration Release
#>
[CmdletBinding()]
param(
    [string]$Repository = '',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build-windows-clang'
)

$ErrorActionPreference = 'Stop'
$transcriptRunning = $false
$log = $null

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Run-Checked([string]$Executable, [string[]]$Arguments, [string]$Step) {
    Write-Host ''
    Write-Host "=== $Step ===" -ForegroundColor Cyan
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed (exit code $LASTEXITCODE)."
    }
}

try {
    if (-not $Repository) {
        $Repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    }
    $Repository = (Resolve-Path -LiteralPath $Repository).Path
    Require-File (Join-Path $Repository 'CMakeLists.txt') 'RTOS Observatory CMakeLists.txt'
    if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
        $BuildDirectory = Join-Path $Repository $BuildDirectory
    }
    $build = [IO.Path]::GetFullPath($BuildDirectory)
    New-Item -ItemType Directory -Path $build -Force | Out-Null
    $log = Join-Path $build 'windows-clang-build.log'
    Start-Transcript -Path $log -Force | Out-Null
    $transcriptRunning = $true

    # Avoid VsDevCmd.bat; the user-reported VS 2026 installation failed to initialize.
    $pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not $pf86) { throw 'ProgramFiles(x86) is not set: expected 64-bit Windows.' }
    $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
    Require-File $vswhere 'Visual Studio vswhere.exe'
    $install = @(& $vswhere -products '*' -latest -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -property installationPath |
        Where-Object { $_ -and $_.Trim() }) | Select-Object -First 1
    if (-not $install) {
        throw 'No installed MSVC x64 tools. Install the Visual Studio Desktop development with C++ workload.'
    }

    $msvcRoot = Join-Path $install 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $msvcRoot -PathType Container)) {
        throw "MSVC toolset directory missing: $msvcRoot"
    }
    $msvcCandidates = @(Get-ChildItem -LiteralPath $msvcRoot -Directory |
        Where-Object { $_.Name -match '^\d+(\.\d+){1,3}$' } |
        Sort-Object { [version]$_.Name } -Descending)
    $msvc = $null
    foreach ($candidate in $msvcCandidates) {
        $lib = Join-Path $candidate.FullName 'lib\x64'
        if ((Test-Path -LiteralPath (Join-Path $lib 'msvcrtd.lib')) -and
            (Test-Path -LiteralPath (Join-Path $lib 'oldnames.lib')) -and
            (Test-Path -LiteralPath (Join-Path $candidate.FullName 'bin\Hostx64\x64\cl.exe'))) {
            $msvc = $candidate
            break
        }
    }
    if (-not $msvc) {
        throw 'MSVC x64 toolset is incomplete (msvcrtd.lib / oldnames.lib / cl.exe). Repair MSVC build tools.'
    }
    $msvcLib = Join-Path $msvc.FullName 'lib\x64'
    $msvcBin = Join-Path $msvc.FullName 'bin\Hostx64\x64'
    $msvcInclude = Join-Path $msvc.FullName 'include'

    $kitCandidates = @()
    $kitReg = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue
    if ($kitReg -and $kitReg.KitsRoot10) { $kitCandidates += $kitReg.KitsRoot10 }
    $kitCandidates += (Join-Path $pf86 'Windows Kits\10')
    $sdk = $null
    foreach ($kit in ($kitCandidates | Select-Object -Unique)) {
        $libRoot = Join-Path $kit 'Lib'
        if (-not (Test-Path -LiteralPath $libRoot -PathType Container)) { continue }
        # Lib\wdf is not a version; never attempt to cast it as one.
        $sdkVersions = @(Get-ChildItem -LiteralPath $libRoot -Directory |
            Where-Object { $_.Name -match '^\d+(\.\d+){1,3}$' } |
            Sort-Object { [version]$_.Name } -Descending)
        foreach ($candidate in $sdkVersions) {
            $v = $candidate.Name
            $sdkUcrt = Join-Path $candidate.FullName 'ucrt\x64'
            $sdkUm = Join-Path $candidate.FullName 'um\x64'
            $sdkBin = Join-Path $kit "bin\$v\x64"
            $sdkInc = Join-Path $kit "Include\$v"
            if ((Test-Path -LiteralPath (Join-Path $sdkUcrt 'ucrt.lib')) -and
                (Test-Path -LiteralPath (Join-Path $sdkUm 'kernel32.lib')) -and
                (Test-Path -LiteralPath (Join-Path $sdkBin 'rc.exe')) -and
                (Test-Path -LiteralPath (Join-Path $sdkBin 'mt.exe')) -and
                (Test-Path -LiteralPath (Join-Path $sdkInc 'um\Windows.h'))) {
                $sdk = @{ Root = $kit; Version = $v; Ucrt = $sdkUcrt; Um = $sdkUm; Bin = $sdkBin; Include = $sdkInc }
                break
            }
        }
        if ($sdk) { break }
    }
    if (-not $sdk) {
        throw 'Complete Windows SDK missing (ucrt, kernel32, Windows.h, rc.exe, mt.exe). Repair the Windows SDK.'
    }

    $clang = 'C:\Program Files\LLVM\bin\clang-cl.exe'
    if (-not (Test-Path -LiteralPath $clang -PathType Leaf)) {
        $clangCommand = Get-Command clang-cl.exe -CommandType Application -ErrorAction SilentlyContinue
        if ($clangCommand) { $clang = @($clangCommand)[0].Source }
    }
    Require-File $clang 'LLVM clang-cl.exe'
    $clangBin = Split-Path $clang -Parent
    Require-File (Join-Path $clangBin 'lld-link.exe') 'LLVM lld-link.exe'
    $cmake = (Get-Command cmake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $ninja = (Get-Command ninja -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
    Require-File $ctest 'CTest executable'

    # Environment is private to this PowerShell process and its child processes.
    $env:VCToolsInstallDir = $msvc.FullName + '\'
    $env:VCToolsVersion = $msvc.Name
    $env:WindowsSdkDir = $sdk.Root.TrimEnd('\') + '\'
    $env:WindowsSDKVersion = $sdk.Version + '\'
    $env:UCRTVersion = $sdk.Version
    $env:Platform = 'x64'
    $env:LIB = (@($msvcLib, $sdk.Ucrt, $sdk.Um, $env:LIB) | Where-Object { $_ }) -join ';'
    $includeDirs = @(
        $msvcInclude,
        (Join-Path $sdk.Include 'ucrt'),
        (Join-Path $sdk.Include 'shared'),
        (Join-Path $sdk.Include 'um'),
        (Join-Path $sdk.Include 'winrt'),
        (Join-Path $sdk.Include 'cppwinrt')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
    $env:INCLUDE = (@($includeDirs) + @($env:INCLUDE) | Where-Object { $_ }) -join ';'
    $env:PATH = (@($msvcBin, $sdk.Bin, $clangBin, $env:PATH) | Where-Object { $_ }) -join ';'

    Write-Host "Visual Studio: $install"
    Write-Host "MSVC: $msvcLib"
    Write-Host "Windows SDK: $($sdk.Version)"
    Write-Host "clang-cl: $clang"
    Write-Host 'VsDevCmd.bat not required'

    # Convert Windows paths to forward slashes before passing them to CMake:
    # its generated compiler files otherwise treat \P as an invalid escape.
    $cmakeArgs = @(
        '-S', $Repository,
        '-B', $build,
        '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$($ninja.Replace('\', '/'))",
        "-DCMAKE_C_COMPILER=$($clang.Replace('\', '/'))",
        "-DCMAKE_CXX_COMPILER=$($clang.Replace('\', '/'))",
        "-DCMAKE_RC_COMPILER=$((Join-Path $sdk.Bin 'rc.exe').Replace('\', '/'))",
        "-DCMAKE_MT=$((Join-Path $sdk.Bin 'mt.exe').Replace('\', '/'))",
        "-DCMAKE_BUILD_TYPE=$Configuration",
        '-DOBSERVATORY_BUILD_DESKTOP=ON',
        '-DOBSERVATORY_BUILD_LIVE=OFF',
        '-DOBSERVATORY_BUILD_TESTS=ON'
    )
    Run-Checked -Executable $cmake -Arguments $cmakeArgs -Step 'CMake configure'
    Run-Checked -Executable $cmake -Arguments @('--build', $build, '--parallel', '4') -Step 'Desktop build'
    Run-Checked -Executable $ctest -Arguments @('--test-dir', $build, '--output-on-failure', '--no-tests=error', '-C', $Configuration) -Step 'CTest'

    Write-Host ''
    Write-Host 'SUCCESS: RTOS Observatory configured, built and passed tests.' -ForegroundColor Green
    Write-Host "Log: $log"
}
catch {
    Write-Host ''
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    if ($log) { Write-Host "Log: $log" }
    exit 1
}
finally {
    if ($transcriptRunning) { Stop-Transcript | Out-Null }
}
