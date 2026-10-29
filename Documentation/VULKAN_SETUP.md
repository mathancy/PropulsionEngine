# Vulkan SDK Installation and Setup Instructions

## Prerequisites

Before building PropulsionMKI with Vulkan, you need to install the Vulkan SDK.

### Windows Installation

1. **Download Vulkan SDK**
   - Visit: https://vulkan.lunarg.com/
   - Download the latest Vulkan SDK installer for Windows
   - Recommended version: 1.3.x or later

2. **Install Vulkan SDK**
   - Run the installer
   - Choose "Complete" installation to get all components
   - The installer will set up the `VULKAN_SDK` environment variable

3. **Verify Installation**
   ```powershell
   # Check environment variable
   $env:VULKAN_SDK
   
   # Should output something like: C:\VulkanSDK\1.3.xxx.x
   
   # Check glslc compiler
   Test-Path "$env:VULKAN_SDK\Bin\glslc.exe"
   
   # Should output: True
   
   # Run vulkaninfo to verify
   & "$env:VULKAN_SDK\Bin\vulkaninfo.exe" --summary
   ```

4. **Restart Your Terminal/IDE**
   - After installation, close and reopen PowerShell/VS Code
   - This ensures environment variables are loaded

### Compile Shaders

Once Vulkan SDK is installed:

```powershell
# Navigate to PropulsionMKI directory
cd g:\PropulsionMKI

# Run shader compilation script
.\compile_shaders.ps1
```

This will compile:
- `shaders/basic.vert` → `shaders/basic_vert.spv`
- `shaders/basic.frag` → `shaders/basic_frag.spv`

### Manual Shader Compilation

If you prefer to compile manually:

```powershell
cd g:\PropulsionMKI\shaders
& "$env:VULKAN_SDK\Bin\glslc.exe" basic.vert -o basic_vert.spv
& "$env:VULKAN_SDK\Bin\glslc.exe" basic.frag -o basic_frag.spv
```

### Troubleshooting

**Issue: VULKAN_SDK not found**
- Solution: Restart your computer after installing Vulkan SDK
- Check System Environment Variables (Windows Settings → System → Advanced → Environment Variables)

**Issue: glslc.exe not found**
- Solution: Reinstall Vulkan SDK with "Complete" option

**Issue: Vulkan validation layers errors**
- Solution: Ensure you have a Vulkan-capable GPU
- Update your GPU drivers to the latest version

### GPU Requirements

PropulsionMKI requires:
- GPU with Vulkan 1.2+ support
- Updated graphics drivers
- Windows 10/11 (64-bit)

Check GPU compatibility:
```powershell
& "$env:VULKAN_SDK\Bin\vulkaninfo.exe" --summary
```

Look for "apiVersion" in the output - it should be 1.2.x or higher.
