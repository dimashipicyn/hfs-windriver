# PowerShell script to build HFS+ driver on Windows 11
param (
    [string]$Arch = "x64",
    [string]$Config = "Release"
)

Write-Host "=== Building HFS+ Driver for Windows 11 ($Arch) ===" -ForegroundColor Cyan

# Check for WinFsp
$WinFspPaths = @(
    "C:\Program Files (x86)\WinFsp",
    "C:\Program Files\WinFsp",
    $env:WINFSP_DIR
)

$WinFspDir = $WinFspPaths | Where-Object { $_ -and (Test-Path "$_\inc\winfsp\winfsp.h") } | Select-Object -First 1

if (-not $WinFspDir) {
    Write-Warning "WinFsp not detected in standard locations!"
    Write-Warning "Please download and install WinFsp from: https://winfsp.dev/"
    Write-Host "Proceeding with core library build only..." -ForegroundColor Yellow
} else {
    Write-Host "Detected WinFsp at: $WinFspDir" -ForegroundColor Green
}

$BuildDir = "build-$Arch"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

$CmakeArgs = @(
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$Config"
)

if ($WinFspDir) {
    $CmakeArgs += "-DWINFSP_DIR=$WinFspDir"
}

cmake @CmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed!"
    exit 1
}

cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed!"
    exit 1
}

Write-Host "=== Build Successful! ===" -ForegroundColor Green
Write-Host "Binary located at: $BuildDir\hfsplus.exe" -ForegroundColor Cyan
