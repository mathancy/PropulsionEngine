# Propulsion MK I Quick Rebuild Script
# Fast clean rebuild for development
# cd G:\PropulsionMKI; .\rebuild.ps1

$ErrorActionPreference = "Stop"
$Host.UI.RawUI.WindowTitle = "Propulsion MK I Rebuild"

try {
    Write-Host "=== Propulsion MK I Clean Rebuild ===" -ForegroundColor Cyan
    Write-Host ""
    
    $engineDir = $PSScriptRoot
    Set-Location $engineDir
    
    # Clean build directory
    if (Test-Path "build") {
        Write-Host "Removing old build directory..." -ForegroundColor Yellow
        Remove-Item -Path "build" -Recurse -Force
        Write-Host "Build directory cleaned" -ForegroundColor Green
    }
    Write-Host ""
    
    # Create fresh build directory
    Write-Host "Creating build directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path "build" | Out-Null
    Set-Location "build"
    Write-Host "Build directory created" -ForegroundColor Green
    Write-Host ""
    
    # Configure with CMake
    Write-Host "Configuring with CMake..." -ForegroundColor Yellow
    $cmakeConfig = cmake .. 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        Write-Host $cmakeConfig
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }
    Write-Host "CMake configuration successful" -ForegroundColor Green
    Write-Host ""
    
    # Build Debug configuration
    Write-Host "Building Propulsion MK I (Debug)..." -ForegroundColor Yellow
    Write-Host "This may take 1-3 minutes..." -ForegroundColor Gray
    Write-Host ""
    
    $buildOutput = cmake --build . --config Debug 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "Build failed!" -ForegroundColor Red
        Write-Host $buildOutput
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }
    
    Write-Host ""
    Write-Host "Build successful!" -ForegroundColor Green
    Write-Host ""
    
    # Compile shaders
    Write-Host "Compiling shaders..." -ForegroundColor Yellow
    Set-Location $engineDir
    $shaderOutput = & "$engineDir\compile_shaders.ps1" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "Shader compilation failed!" -ForegroundColor Red
        Write-Host $shaderOutput
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }
    Write-Host "Shaders compiled successfully" -ForegroundColor Green
    Write-Host ""
    
    # Done
    Write-Host "=== REBUILD COMPLETE ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "To run: cd `"$engineDir`" ; .\PropulsionMKI.exe" -ForegroundColor Yellow
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Host "FATAL ERROR - REBUILD FAILED" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host ""
    Write-Host "Press Enter to exit..."
    Read-Host
    exit 1
}
