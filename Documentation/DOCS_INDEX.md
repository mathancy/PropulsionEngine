# Forge Engine - Complete Documentation Index

Welcome to the Forge Engine documentation. This index helps you find the information you need quickly.

---

## 📖 Getting Started

### New Users
1. **[README.md](README.md)** - Start here! Overview and features
2. **[QUICKSTART.md](QUICKSTART.md)** - Get running in 10 minutes
3. **[BUILD.md](BUILD.md)** - Detailed build instructions
4. **[SETUP.md](SETUP.md)** - Dependency setup guide

### Experiencing Issues?
- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** - Common problems and solutions

---

## 📚 Documentation Files

### Core Documentation

| File | Purpose | When to Read |
|------|---------|--------------|
| **[README.md](README.md)** | Project overview, features, quick reference | First time, general info |
| **[QUICKSTART.md](QUICKSTART.md)** | Fast setup guide with step-by-step instructions | Want to run engine quickly |
| **[BUILD.md](BUILD.md)** | Comprehensive build process and troubleshooting | Building for first time |
| **[SETUP.md](SETUP.md)** | Detailed dependency setup (GLFW, GLAD, GLM, ImGui) | Setting up dev environment |

### Advanced Documentation

| File | Purpose | When to Read |
|------|---------|--------------|
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | System design, roadmap, technical decisions | Understanding codebase |
| **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** | Solutions to common problems | Something's broken |
| **[LICENSE](LICENSE)** | Legal terms and usage restrictions | Legal/compliance questions |

### Project Files

| File | Purpose |
|------|---------|
| **CMakeLists.txt** | Build configuration |
| **setup.ps1** | Automated dependency setup script |
| **.gitignore** | Git exclusions |
| **.gitattributes** | Git file handling |
| **ForgeEngine.code-workspace** | VS Code workspace configuration |

---

## 🗂️ Documentation by Topic

### Installation & Setup

1. Prerequisites check → **[BUILD.md](BUILD.md#phase-1-verify-prerequisites)**
2. Download dependencies → **[QUICKSTART.md](QUICKSTART.md#step-1-run-the-setup-script)**
3. Setup GLAD → **[SETUP.md](SETUP.md#2-glad-opengl-loader)**
4. Build project → **[BUILD.md](BUILD.md#phase-3-build-with-cmake)**
5. Run engine → **[QUICKSTART.md](QUICKSTART.md#step-4-run-forge-engine)**

### Usage

- **Controls** → [README.md](README.md#-controls)
- **Camera movement** → [QUICKSTART.md](QUICKSTART.md#controls)
- **UI interaction** → [README.md](README.md#what-youll-see)
- **Expected behavior** → [QUICKSTART.md](QUICKSTART.md#expected-result)

### Development

- **Project structure** → [README.md](README.md#-project-structure)
- **Code architecture** → [ARCHITECTURE.md](ARCHITECTURE.md#architecture-overview)
- **Class hierarchy** → [ARCHITECTURE.md](ARCHITECTURE.md#current-class-structure)
- **Adding features** → [ARCHITECTURE.md](ARCHITECTURE.md#contributing)
- **Code style** → [ARCHITECTURE.md](ARCHITECTURE.md#code-style)

### Troubleshooting

- **Build errors** → [TROUBLESHOOTING.md](TROUBLESHOOTING.md#build-issues)
- **Runtime crashes** → [TROUBLESHOOTING.md](TROUBLESHOOTING.md#runtime-issues)
- **Performance issues** → [TROUBLESHOOTING.md](TROUBLESHOOTING.md#performance-issues)
- **ImGui problems** → [TROUBLESHOOTING.md](TROUBLESHOOTING.md#imgui-issues)

### Planning & Roadmap

- **Current features** → [README.md](README.md#-features)
- **Future features** → [ARCHITECTURE.md](ARCHITECTURE.md#planned-features)
- **Development phases** → [ARCHITECTURE.md](ARCHITECTURE.md#development-priorities)
- **Visual scripting vision** → [ARCHITECTURE.md](ARCHITECTURE.md#visual-scripting-vision)
- **ForgeScript language and node editor** → `ForgeScript/SPEC.md` (the spec) and `ForgeScript/cpp/README.md` (how the engine runs it)
- **Which Unreal nodes we have, and what they are called here** → [NODELIST.md](NODELIST.md) and the coverage table in `ForgeScript/SPEC.md`

---

## 🎯 Quick Links by Role

### For New Developers
1. Read [README.md](README.md)
2. Follow [QUICKSTART.md](QUICKSTART.md)
3. Review [ARCHITECTURE.md](ARCHITECTURE.md)
4. Study code in `src/` and `include/`

### For Artists/Designers
1. Read [QUICKSTART.md](QUICKSTART.md)
2. Learn [Controls](README.md#-controls)
3. Review [Visual Scripting Vision](ARCHITECTURE.md#visual-scripting-vision)

### For Team Leads
1. Review [ARCHITECTURE.md](ARCHITECTURE.md)
2. Check [Roadmap](ARCHITECTURE.md#development-priorities)
3. Read [LICENSE](LICENSE)
4. Review [Technical Goals](ARCHITECTURE.md#technical-goals)

### For System Administrators
1. Check [Prerequisites](BUILD.md#phase-1-verify-prerequisites)
2. Review [Dependencies](SETUP.md)
3. Verify [Troubleshooting Guide](TROUBLESHOOTING.md)

---

## 📝 File Descriptions

### README.md (Main)
- **Size**: Medium
- **Read Time**: 3-5 minutes
- **Content**: Project overview, feature list, quick setup, technology stack
- **Audience**: Everyone

### QUICKSTART.md
- **Size**: Medium
- **Read Time**: 5-10 minutes
- **Content**: Step-by-step setup, controls, expected results
- **Audience**: New users wanting to run quickly

### BUILD.md
- **Size**: Large
- **Read Time**: 10-15 minutes
- **Content**: Complete build process, troubleshooting, advanced options
- **Audience**: Developers setting up build environment

### SETUP.md
- **Size**: Medium
- **Read Time**: 5-10 minutes
- **Content**: Detailed dependency setup for each library
- **Audience**: Developers needing dependency details

### ARCHITECTURE.md
- **Size**: Very Large
- **Read Time**: 20-30 minutes
- **Content**: System design, roadmap, technical decisions, future plans
- **Audience**: Developers, architects, leads

### TROUBLESHOOTING.md
- **Size**: Large
- **Read Time**: As needed (reference)
- **Content**: Common problems and solutions
- **Audience**: Anyone experiencing issues

### LICENSE
- **Size**: Medium
- **Read Time**: 5-10 minutes
- **Content**: Legal terms, usage restrictions
- **Audience**: Legal, compliance, management

---

## 🔍 Search Guide

### I want to...

**...install and run the engine**
→ [QUICKSTART.md](QUICKSTART.md)

**...understand how to build from source**
→ [BUILD.md](BUILD.md)

**...set up GLFW, GLAD, GLM, or ImGui**
→ [SETUP.md](SETUP.md)

**...fix a build error**
→ [TROUBLESHOOTING.md](TROUBLESHOOTING.md#build-issues)

**...fix a runtime error**
→ [TROUBLESHOOTING.md](TROUBLESHOOTING.md#runtime-issues)

**...understand the code architecture**
→ [ARCHITECTURE.md](ARCHITECTURE.md#architecture-overview)

**...see what's planned for the future**
→ [ARCHITECTURE.md](ARCHITECTURE.md#planned-features)

**...contribute code**
→ [ARCHITECTURE.md](ARCHITECTURE.md#contributing)

**...know the license terms**
→ [LICENSE](LICENSE)

**...learn the controls**
→ [README.md](README.md#-controls) or [QUICKSTART.md](QUICKSTART.md#controls)

---

## 📋 Cheat Sheets

### Build Commands

```powershell
# Initial setup
.\setup.ps1

# Build (Release)
mkdir build; cd build
cmake ..
cmake --build . --config Release

# Build (Debug)
cmake --build . --config Debug

# Clean rebuild
rm -r build; mkdir build; cd build; cmake ..; cmake --build . --config Release

# Run
.\bin\Release\ForgeEngine.exe
```

### Controls

| Action | Input |
|--------|-------|
| Look around | Right mouse + move |
| Move | W/A/S/D |
| Up/Down | E/Q |
| Sprint | Hold Shift |
| Exit | ESC |

### Project Structure

```
ForgeEngine/
├── src/              → Implementation (.cpp)
├── include/          → Headers (.h)
├── external/         → Dependencies
├── build/            → Build output
└── *.md              → Documentation
```

---

## 🆘 Getting Help

### Self-Help Resources
1. Check relevant documentation file
2. Search TROUBLESHOOTING.md
3. Review error messages carefully
4. Try clean rebuild

### Team Support
For Forgeworks Interactive employees:
- Contact development team
- Include error messages
- Describe steps to reproduce
- Mention your environment (OS, compiler, etc.)

---

## 📱 Documentation Updates

This documentation is maintained by the Forgeworks Interactive development team.

**Last Updated**: November 25, 2025

### Contributing to Docs
When updating code, please also update relevant documentation:
- Add new features to README.md and ARCHITECTURE.md
- Document new build steps in BUILD.md
- Add troubleshooting entries as issues arise
- Keep roadmap current in ARCHITECTURE.md

---

**Forgeworks Interactive Limited**  
*Building Tools for Tomorrow's Games*
