# Optihus smoke (items 1 + 4): stub miljoestyring + local server. Needs a C++ toolchain (e.g. VS or Ninja+Clang).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build-smoke-stub"
$CfgExample = Join-Path $Root "miljoestyring_config.json.example"
$CfgDst = Join-Path $Build "miljoestyring_config.json"

cmake -S $Root -B $Build `
  -DCMAKE_BUILD_TYPE=Release `
  -DOPTIHUS_MILJOESTYRING_HARDWARE=OFF `
  -DOPTIHUS_BUILD_DATAHAANDTERING=ON `
  -DOPTIHUS_BUILD_MILJOESTYRING=ON

cmake --build $Build --parallel --target miljoestyring datahaandtering

New-Item -ItemType Directory -Force -Path $Build | Out-Null
Copy-Item -Force $CfgExample $CfgDst
(Get-Content $CfgDst -Raw) -replace '"192\.168\.4\.1"', '"127.0.0.1"' | Set-Content $CfgDst -NoNewline

Write-Host ""
Write-Host "Terminal A: cd `"$Build`" ; .\datahaandtering.exe   (or .\datahaandtering)"
Write-Host "Terminal B: cd `"$Build`" ; .\miljoestyring.exe  (stub, no GPIO)"
Write-Host "Config: $CfgDst"
