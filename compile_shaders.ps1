# Shader Compilation Script
# This script compiles GLSL shaders to SPIR-V bytecode

Write-Host "Compiling shaders to SPIR-V..." -ForegroundColor Cyan

# Check if Vulkan SDK is installed
if (-not $env:VULKAN_SDK) {
    Write-Host "ERROR: VULKAN_SDK environment variable is not set!" -ForegroundColor Red
    Write-Host "Please install the Vulkan SDK from: https://vulkan.lunarg.com/" -ForegroundColor Yellow
    Write-Host "After installation, restart your terminal/IDE." -ForegroundColor Yellow
    exit 1
}

$glslc = "$env:VULKAN_SDK\Bin\glslc.exe"

if (-not (Test-Path $glslc)) {
    Write-Host "ERROR: glslc compiler not found at: $glslc" -ForegroundColor Red
    Write-Host "Please ensure Vulkan SDK is properly installed." -ForegroundColor Yellow
    exit 1
}

Write-Host "Using glslc: $glslc" -ForegroundColor Green

# Change to shaders directory
Set-Location -Path "$PSScriptRoot\shaders"

# Compile vertex shader
Write-Host "Compiling basic.vert..." -ForegroundColor Cyan
& $glslc basic.vert -o basic_vert.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS basic.vert compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile basic.vert" -ForegroundColor Red
    exit 1
}

# Compile fragment shader
Write-Host "Compiling basic.frag..." -ForegroundColor Cyan
& $glslc basic.frag -o basic_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS basic.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile basic.frag" -ForegroundColor Red
    exit 1
}

# Compile grid vertex shader
Write-Host "Compiling grid.vert..." -ForegroundColor Cyan
& $glslc grid.vert -o grid_vert.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS grid.vert compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile grid.vert" -ForegroundColor Red
    exit 1
}

# Compile grid fragment shader
Write-Host "Compiling grid.frag..." -ForegroundColor Cyan
& $glslc grid.frag -o grid_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS grid.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile grid.frag" -ForegroundColor Red
    exit 1
}

# Compile mask vertex shader
Write-Host "Compiling mask.vert..." -ForegroundColor Cyan
& $glslc mask.vert -o mask_vert.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS mask.vert compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile mask.vert" -ForegroundColor Red
    exit 1
}

# Compile mask fragment shader
Write-Host "Compiling mask.frag..." -ForegroundColor Cyan
& $glslc mask.frag -o mask_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS mask.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile mask.frag" -ForegroundColor Red
    exit 1
}

# Compile fullscreen vertex shader (for post-processing)
Write-Host "Compiling fullscreen.vert..." -ForegroundColor Cyan
& $glslc fullscreen.vert -o fullscreen_vert.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS fullscreen.vert compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile fullscreen.vert" -ForegroundColor Red
    exit 1
}

# Compile horizontal dilation shader
Write-Host "Compiling dilate_h.frag..." -ForegroundColor Cyan
& $glslc dilate_h.frag -o dilate_h_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS dilate_h.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile dilate_h.frag" -ForegroundColor Red
    exit 1
}

# Compile vertical dilation shader
Write-Host "Compiling dilate_v.frag..." -ForegroundColor Cyan
& $glslc dilate_v.frag -o dilate_v_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS dilate_v.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile dilate_v.frag" -ForegroundColor Red
    exit 1
}

# Compile composite shader (combines outline extraction and scene blending)
Write-Host "Compiling composite.frag..." -ForegroundColor Cyan
& $glslc composite.frag -o composite_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS composite.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile composite.frag" -ForegroundColor Red
    exit 1
}

# Compile gizmo vertex shader
Write-Host "Compiling gizmo.vert..." -ForegroundColor Cyan
& $glslc gizmo.vert -o gizmo_vert.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS gizmo.vert compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile gizmo.vert" -ForegroundColor Red
    exit 1
}

# Compile gizmo fragment shader
Write-Host "Compiling gizmo.frag..." -ForegroundColor Cyan
& $glslc gizmo.frag -o gizmo_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS gizmo.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile gizmo.frag" -ForegroundColor Red
    exit 1
}

# Compile gizmo blend fragment shader
Write-Host "Compiling gizmo_blend.frag..." -ForegroundColor Cyan
& $glslc gizmo_blend.frag -o gizmo_blend_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS gizmo_blend.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile gizmo_blend.frag" -ForegroundColor Red
    exit 1
}

# Compile thumbnail vertex shader
Write-Host "Compiling thumbnail.vert..." -ForegroundColor Cyan
& $glslc thumbnail.vert -o thumbnail_vert.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS thumbnail.vert compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile thumbnail.vert" -ForegroundColor Red
    exit 1
}

# Compile thumbnail fragment shader
Write-Host "Compiling thumbnail.frag..." -ForegroundColor Cyan
& $glslc thumbnail.frag -o thumbnail_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS thumbnail.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile thumbnail.frag" -ForegroundColor Red
    exit 1
}

# Compile sky vertex shader (the environment drawn behind the scene)
Write-Host "Compiling sky.vert..." -ForegroundColor Cyan
& $glslc sky.vert -o sky_vert.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS sky.vert compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile sky.vert" -ForegroundColor Red
    exit 1
}

# Compile sky fragment shader (includes lighting.glsl, as basic.frag does)
Write-Host "Compiling sky.frag..." -ForegroundColor Cyan
& $glslc sky.frag -o sky_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS sky.frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile sky.frag" -ForegroundColor Red
    exit 1
}

# Shadows and ambient occlusion
foreach ($shader in @("shadow.vert", "prepass.vert", "prepass.frag", "ssao.frag", "ssao_blur.frag")) {
    Write-Host "Compiling $shader..." -ForegroundColor Cyan
    & $glslc $shader -o "$($shader.Replace('.vert', '_vert').Replace('.frag', '_frag')).spv"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  SUCCESS $shader compiled" -ForegroundColor Green
    } else {
        Write-Host "  FAILED to compile $shader" -ForegroundColor Red
        exit 1
    }
}

# Ray tracing shaders (need SPIR-V 1.4 or later, so Vulkan 1.2 or later as the target)
Write-Host "Compiling basic.frag (Hybrid RT variant)..." -ForegroundColor Cyan
& $glslc --target-env=vulkan1.2 -DHYBRID_RT basic.frag -o basic_rt_frag.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS basic_rt_frag compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile basic.frag with HYBRID_RT" -ForegroundColor Red
    exit 1
}
Write-Host "Compiling rt_debug.comp..." -ForegroundColor Cyan
& $glslc --target-env=vulkan1.3 rt_debug.comp -o rt_debug_comp.spv
if ($LASTEXITCODE -eq 0) {
    Write-Host "  SUCCESS rt_debug.comp compiled" -ForegroundColor Green
} else {
    Write-Host "  FAILED to compile rt_debug.comp" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "All shaders compiled successfully!" -ForegroundColor Green
Write-Host "Output files:" -ForegroundColor Cyan
Write-Host "  - basic_vert.spv"
Write-Host "  - basic_frag.spv"
Write-Host "  - grid_vert.spv"
Write-Host "  - grid_frag.spv"
Write-Host "  - mask_vert.spv"
Write-Host "  - mask_frag.spv"
Write-Host "  - fullscreen_vert.spv"
Write-Host "  - dilate_h_frag.spv"
Write-Host "  - dilate_v_frag.spv"
Write-Host "  - composite_frag.spv"
Write-Host "  - gizmo_vert.spv"
Write-Host "  - gizmo_frag.spv"
Write-Host "  - gizmo_blend_frag.spv"
Write-Host "  - thumbnail_vert.spv"
Write-Host "  - thumbnail_frag.spv"
Write-Host "  - sky_vert.spv"
Write-Host "  - sky_frag.spv"
Write-Host "  - shadow_vert.spv, prepass_vert.spv, prepass_frag.spv, ssao_frag.spv, ssao_blur_frag.spv"
Write-Host "  - basic_rt_frag.spv, rt_debug_comp.spv"
