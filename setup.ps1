# Propulsion MK I Setup Script
# This script automatically sets up and builds the complete engine

$ErrorActionPreference = "Stop"
$Host.UI.RawUI.WindowTitle = "Propulsion MK I Setup"

Start-Transcript -Path "$PSScriptRoot\setup_log.txt" -Force

# Define shared dependencies location
$documentsPath = [Environment]::GetFolderPath("MyDocuments")
$propulsionSharedDir = Join-Path $documentsPath "Forgeworks Interactive\Propulsion MK I\Dependencies"

function Select-FolderDialog {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $folderBrowser = New-Object System.Windows.Forms.FolderBrowserDialog
        $folderBrowser.Description = "Select installation directory for Propulsion MK I"
        $folderBrowser.RootFolder = [System.Environment+SpecialFolder]::MyComputer
        $folderBrowser.ShowNewFolderButton = $true
        
        if ($folderBrowser.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            return $folderBrowser.SelectedPath
        }
        return $null
    }
    catch {
        Write-Host "Error creating folder dialog: $_" -ForegroundColor Red
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }
}

function Update-Repository {
    param(
        [string]$name,
        [string]$url,
        [string]$branch = "",
        [string]$expectedVersion = ""
    )
    
    $repoPath = Join-Path $propulsionSharedDir $name
    $needsClone = $false
    
    if (Test-Path $repoPath) {
        Write-Host "Checking $name..." -ForegroundColor Yellow
        Set-Location $repoPath
        
        # Check if it's a valid git repository
        $isGitRepo = Test-Path ".git"
        
        if ($isGitRepo -and $expectedVersion) {
            # Get current commit hash
            $currentCommit = git rev-parse HEAD 2>$null
            $latestCommit = git ls-remote origin HEAD 2>$null | ForEach-Object { $_.Split()[0] }
            
            if ($currentCommit -eq $latestCommit) {
                Write-Host "$name is up to date" -ForegroundColor Green
                return $repoPath
            } else {
                Write-Host "Updating $name..." -ForegroundColor Yellow
                git pull
            }
        } elseif (-not $isGitRepo) {
            Write-Host "$name directory exists but is not a git repository, re-cloning..." -ForegroundColor Yellow
            Set-Location $propulsionSharedDir
            Remove-Item -Path $repoPath -Recurse -Force
            $needsClone = $true
        } else {
            Write-Host "$name found" -ForegroundColor Green
        }
    } else {
        $needsClone = $true
    }
    
    if ($needsClone) {
        Set-Location $propulsionSharedDir
        Write-Host "Cloning $name..." -ForegroundColor Green
        if ($branch) {
            git clone --branch $branch --depth 1 $url $name
        } else {
            git clone --depth 1 $url $name
        }
    }
    
    return $repoPath
}

try {
    Write-Host "=== Propulsion MK I Complete Setup ===" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Please select installation directory..." -ForegroundColor Yellow
    $installDir = Select-FolderDialog

    if (-not $installDir) {
        Write-Host ""
        Write-Host "Installation cancelled by user." -ForegroundColor Red
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }

    $engineDir = Join-Path $installDir "PropulsionMKI"
    Write-Host ""
    Write-Host "Installing Propulsion MK I to: $engineDir" -ForegroundColor Green
    Write-Host ""

    # Remove existing installation if it exists
    if (Test-Path $engineDir) {
        Write-Host "Existing PropulsionMKI installation found, removing..." -ForegroundColor Yellow
        Remove-Item -Path $engineDir -Recurse -Force
        Write-Host "Old installation removed" -ForegroundColor Green
        Write-Host ""
    }

    New-Item -ItemType Directory -Path $engineDir -Force | Out-Null

    Write-Host "Copying engine files..." -ForegroundColor Yellow
    $scriptDir = $PSScriptRoot

    @("src", "include", "external", "Documentation", "resources", "shaders", "scripts") | ForEach-Object {
        $sourcePath = Join-Path $scriptDir $_
        $destPath = Join-Path $engineDir $_
        if (Test-Path $sourcePath) {
            Copy-Item -Path $sourcePath -Destination $destPath -Recurse -Force
        }
    }

    @("CMakeLists.txt", ".gitignore", ".gitattributes", "PropulsionMKI.code-workspace", "LICENSE", "compile_shaders.ps1") | ForEach-Object {
        $sourceFile = Join-Path $scriptDir $_
        if (Test-Path $sourceFile) {
            Copy-Item -Path $sourceFile -Destination $engineDir -Force
        }
    }

    Write-Host "Engine files copied" -ForegroundColor Green
    Write-Host ""
    
    # Create shared dependencies directory
    Write-Host "Setting up shared dependencies in: $propulsionSharedDir" -ForegroundColor Cyan
    if (-not (Test-Path $propulsionSharedDir)) {
        New-Item -ItemType Directory -Path $propulsionSharedDir -Force | Out-Null
    }
    
    Set-Location $engineDir

    $externalDir = Join-Path $engineDir "external"
    if (-not (Test-Path $externalDir)) {
        New-Item -ItemType Directory -Path $externalDir | Out-Null
    }

    Write-Host ""
    Write-Host "[1/5] Setting up GLFW..." -ForegroundColor Cyan
    $glfwPath = Update-Repository -name "glfw" -url "https://github.com/glfw/glfw.git"
    
    # Create symbolic link or copy to external
    $glfwLink = Join-Path $externalDir "glfw"
    if (Test-Path $glfwLink) {
        Remove-Item -Path $glfwLink -Recurse -Force
    }
    Write-Host "Linking GLFW to project..." -ForegroundColor Yellow
    $junctionResult = cmd /c mklink /J "`"$glfwLink`"" "`"$glfwPath`"" 2>&1
    if (-not (Test-Path $glfwLink)) {
        Write-Host "Junction failed, copying instead..." -ForegroundColor Yellow
        Copy-Item -Path $glfwPath -Destination $glfwLink -Recurse -Force
    }

    Write-Host ""
    Write-Host "[2/5] Setting up GLM..." -ForegroundColor Cyan
    $glmPath = Update-Repository -name "glm" -url "https://github.com/g-truc/glm.git"
    
    $glmLink = Join-Path $externalDir "glm"
    if (Test-Path $glmLink) {
        Remove-Item -Path $glmLink -Recurse -Force
    }
    Write-Host "Linking GLM to project..." -ForegroundColor Yellow
    $junctionResult = cmd /c mklink /J "`"$glmLink`"" "`"$glmPath`"" 2>&1
    if (-not (Test-Path $glmLink)) {
        Write-Host "Junction failed, copying instead..." -ForegroundColor Yellow
        Copy-Item -Path $glmPath -Destination $glmLink -Recurse -Force
    }

    Write-Host ""
    Write-Host "[3/5] Setting up Dear ImGui (docking branch)..." -ForegroundColor Cyan
    $imguiPath = Update-Repository -name "imgui" -url "https://github.com/ocornut/imgui.git" -branch "docking"
    
    $imguiLink = Join-Path $externalDir "imgui"
    if (Test-Path $imguiLink) {
        Remove-Item -Path $imguiLink -Recurse -Force
    }
    Write-Host "Linking ImGui to project..." -ForegroundColor Yellow
    $junctionResult = cmd /c mklink /J "`"$imguiLink`"" "`"$imguiPath`"" 2>&1
    if (-not (Test-Path $imguiLink)) {
        Write-Host "Junction failed, copying instead..." -ForegroundColor Yellow
        Copy-Item -Path $imguiPath -Destination $imguiLink -Recurse -Force
    }

    Write-Host ""
    Write-Host "[4/5] Setting up GLAD..." -ForegroundColor Cyan

    $gladDir = Join-Path $externalDir "glad"
    $gladDir = Join-Path $externalDir "glad"
    $sharedGladDir = Join-Path $propulsionSharedDir "glad"
    
    if (-not (Test-Path $sharedGladDir)) {
        Write-Host "GLAD not found in shared location, checking source..." -ForegroundColor Yellow
        
        $sourceGladDir = Join-Path $scriptDir "external\glad"
        if (Test-Path $sourceGladDir) {
            Write-Host "Copying GLAD to shared location..." -ForegroundColor Yellow
            Copy-Item -Path $sourceGladDir -Destination $sharedGladDir -Recurse -Force
            Write-Host "GLAD copied successfully" -ForegroundColor Green
        } else {
            Write-Host "GLAD not found in source directory!" -ForegroundColor Red
            Write-Host ""
            Write-Host "GLAD must be in G:\PropulsionMKI\external\glad\" -ForegroundColor Yellow
            Set-Location $engineDir
            # explorer.exe $engineDir
            Write-Host ""
            Write-Host "Press Enter to exit..."
            Read-Host
            exit 1
        }
    } else {
        Write-Host "GLAD found in shared location" -ForegroundColor Green
    }
    
    # Link GLAD to project
    if (Test-Path $gladDir) {
        Remove-Item -Path $gladDir -Recurse -Force
    }
    Write-Host "Linking GLAD to project..." -ForegroundColor Yellow
    $junctionResult = cmd /c mklink /J "`"$gladDir`"" "`"$sharedGladDir`"" 2>&1
    if (-not (Test-Path $gladDir)) {
        Write-Host "Junction failed, copying instead..." -ForegroundColor Yellow
        Copy-Item -Path $sharedGladDir -Destination $gladDir -Recurse -Force
    }

    if (-not (Test-Path "$gladDir/include/glad/glad.h")) {
        Write-Host ""
        Write-Host "GLAD structure is incomplete!" -ForegroundColor Red
        Write-Host "Missing: $gladDir/include/glad/glad.h" -ForegroundColor Yellow
        Write-Host ""
        Set-Location $engineDir
        # explorer.exe $engineDir
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }

    Write-Host "GLAD verification successful" -ForegroundColor Green

    Write-Host ""
    Write-Host "[5/5] Setting up LunaSVG..." -ForegroundColor Cyan
    $lunasvgPath = Update-Repository -name "lunasvg" -url "https://github.com/sammycage/lunasvg.git"
    
    $lunasvgLink = Join-Path $externalDir "lunasvg"
    if (Test-Path $lunasvgLink) {
        Remove-Item -Path $lunasvgLink -Recurse -Force
    }
    Write-Host "Linking LunaSVG to project..." -ForegroundColor Yellow
    $junctionResult = cmd /c mklink /J "`"$lunasvgLink`"" "`"$lunasvgPath`"" 2>&1
    if (-not (Test-Path $lunasvgLink)) {
        Write-Host "Junction failed, copying instead..." -ForegroundColor Yellow
        Copy-Item -Path $lunasvgPath -Destination $lunasvgLink -Recurse -Force
    }

    Write-Host ""
    Write-Host "=== Dependencies Complete! ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "Shared dependencies location: $propulsionSharedDir" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "[BUILDING ENGINE]" -ForegroundColor Cyan
    Write-Host ""

    Set-Location $engineDir

    Write-Host "Creating build directory..." -ForegroundColor Yellow
    if (Test-Path "build") {
        Remove-Item -Path "build" -Recurse -Force
    }
    New-Item -ItemType Directory -Path "build" | Out-Null
    Set-Location "build"

    Write-Host "Build directory created" -ForegroundColor Green
    Write-Host ""

    Write-Host "Configuring with CMake..." -ForegroundColor Yellow
    $cmakeConfig = cmake .. 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        Write-Host ""
        Write-Host "Error output:" -ForegroundColor Yellow
        Write-Host $cmakeConfig
        Write-Host ""
        Write-Host "Please check that:" -ForegroundColor Cyan
        Write-Host "  1. CMake is installed and in PATH" -ForegroundColor White
        Write-Host "  2. Visual Studio C++ tools are installed" -ForegroundColor White
        Write-Host "  3. GLAD was properly set up" -ForegroundColor White
        Write-Host ""
        Set-Location $engineDir
        # explorer.exe $engineDir
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }

    Write-Host "CMake configuration successful" -ForegroundColor Green
    Write-Host ""

    Write-Host "Building Propulsion MK I (Release)..." -ForegroundColor Yellow
    Write-Host "This may take 2-5 minutes..." -ForegroundColor Gray
    Write-Host ""

    $buildOutput = cmake --build . --config Release 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "Build failed!" -ForegroundColor Red
        Write-Host ""
        Write-Host "Error output:" -ForegroundColor Yellow
        Write-Host $buildOutput
        Write-Host ""
        Set-Location $engineDir
        # explorer.exe $engineDir
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
        exit 1
    }

    Write-Host ""
    Write-Host "Build successful!" -ForegroundColor Green
    Write-Host ""

    Write-Host "Compiling shaders..." -ForegroundColor Yellow
    Set-Location $engineDir
    $shaderCompileOutput = & "$engineDir\compile_shaders.ps1" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "Shader compilation failed!" -ForegroundColor Red
        Write-Host $shaderCompileOutput
        Write-Host ""
        Write-Host "Note: Engine will crash without compiled shaders!" -ForegroundColor Yellow
        Write-Host ""
    } else {
        Write-Host "Shaders compiled successfully" -ForegroundColor Green
        Write-Host ""
    }

    Set-Location $engineDir

    $exePath = Join-Path $engineDir "PropulsionMKI.exe"
    
    if (Test-Path $exePath) {
        Write-Host "PROPULSION MK I SETUP COMPLETE!" -ForegroundColor Green
        Write-Host ""
        Write-Host "Installation Directory:" -ForegroundColor Cyan
        Write-Host "  $engineDir" -ForegroundColor White
        Write-Host ""
        Write-Host "Executable Location:" -ForegroundColor Cyan
        Write-Host "  $exePath" -ForegroundColor White
        Write-Host ""
        Write-Host "To run: cd `"$engineDir`" ; .\PropulsionMKI.exe" -ForegroundColor Yellow
        Write-Host ""
        Start-Sleep -Seconds 2
        # explorer.exe $engineDir
        Write-Host ""
        Write-Host "Setup complete! Closing in 3 seconds..." -ForegroundColor Green
        Start-Sleep -Seconds 3
    } else {
        Write-Host "Build completed but executable not found." -ForegroundColor Yellow
        # explorer.exe $engineDir
        Write-Host ""
        Write-Host "Press Enter to exit..."
        Read-Host
    }

} catch {
    Write-Host ""
    Write-Host "FATAL ERROR - SETUP FAILED" -ForegroundColor Red
    Write-Host ""
    Write-Host "Error Details:" -ForegroundColor Yellow
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host ""
    Write-Host "Stack Trace:" -ForegroundColor Yellow
    Write-Host $_.ScriptStackTrace -ForegroundColor Gray
    Write-Host ""
    Write-Host "Check setup_log.txt for details" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Press Enter to exit..."
    Read-Host
    exit 1
} finally {
    Stop-Transcript
}
