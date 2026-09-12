# Downloads the prebuilt Slint C++ SDK into third_party\slint.
#
# The prebuilt package is used so the build needs no Rust toolchain. The Windows
# package is an MSVC build, so the launcher must also be built with MSVC; for
# MinGW, build Slint from source (see docs/BUILDING.md).
#
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_slint.ps1

param([string]$SlintVersion = "1.17.1")
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\slint"

$arch = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") { "ARM64" } else { "AMD64" }
$installer = "Slint-cpp-$SlintVersion-win64-MSVC-$arch.exe"
$url = "https://github.com/slint-ui/slint/releases/download/v$SlintVersion/$installer"

$tmp = Join-Path $env:TEMP "slint-$SlintVersion"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$exe = Join-Path $tmp $installer

Write-Host "Fetching $url"
Invoke-WebRequest -Uri $url -OutFile $exe

# The release is an NSIS self-extractor; /S is silent, /D sets the target (must
# be last and unquoted, which is an NSIS requirement, not a typo).
Write-Host "Installing to $dest"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
Start-Process -FilePath $exe -ArgumentList "/S", "/D=$dest" -Wait -NoNewWindow

Write-Host "Slint $SlintVersion installed in $dest"
