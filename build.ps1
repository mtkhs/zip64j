# zip64j build script (CMake + VS2022 x64)
param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [Parameter(Mandatory=$false)]
    [switch]$Rebuild,

    [Parameter(Mandatory=$false)]
    [switch]$Reconfigure
)

$ErrorActionPreference = "Stop"

Write-Host "Building zip64j - Configuration: $Configuration" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$BuildDir = "build"

if ($Reconfigure -and (Test-Path $BuildDir)) {
    Write-Host "Removing existing $BuildDir for reconfigure..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "Configuring CMake..." -ForegroundColor Yellow
    cmake -B $BuildDir -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { Write-Host "CMake configure failed" -ForegroundColor Red; exit 1 }
}

Write-Host "Building..." -ForegroundColor Yellow
if ($Rebuild) {
    cmake --build $BuildDir --config $Configuration --clean-first
} else {
    cmake --build $BuildDir --config $Configuration
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build successful!" -ForegroundColor Green
    $DistDir = Join-Path $BuildDir "dist"
    if (Test-Path $DistDir) {
        Get-ChildItem $DistDir -Filter "*.dll" | ForEach-Object {
            Write-Host "  - $($_.Name) ($('{0:N0}' -f $_.Length) bytes)" -ForegroundColor Green
        }
    }
} else {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
