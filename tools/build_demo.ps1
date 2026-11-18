# SPDX-License-Identifier: Apache-2.0
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
    ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    $exe = @('.\build\Release\rtos-observatory.exe', '.\build\rtos-observatory.exe') | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exe) { throw 'Built executable not found' }
    & $exe --input .\examples\inversion.trace --output .\examples\my-report.json
    if ($LASTEXITCODE -ne 0) { throw 'Demo capture failed' }
    Write-Host 'OPEN THE VIEWER:' (Join-Path $root 'viewer\index.html')
    Write-Host 'Use Open report JSON to choose examples\my-report.json'
} finally { Pop-Location }
