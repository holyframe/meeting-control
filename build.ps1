param(
    [Parameter(Mandatory = $false)]
    [string]$QtPath = "C:\Qt\6.8.3\msvc2022_64"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $QtPath)) {
    throw "Qt MSVC kit not found at '$QtPath'. Pass the correct Qt installation path."
}

cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot "build") -DCMAKE_PREFIX_PATH="$QtPath"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

cmake --build (Join-Path $PSScriptRoot "build") --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

Write-Host "Build complete: $PSScriptRoot\build\Release\MeetingController.exe"
