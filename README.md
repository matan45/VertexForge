# VertexForge

![image](https://github.com/matan45/VertexForge/blob/dev/resources/editor/VertexForge-logo.png)

## Introduction
VertexForge is a modular game engine framework built in C++ using modern technologies such as Vulkan, GLFW, ImGui, and spdlog. The project is structured to support the development of real-time applications, with components divided into several sub-projects including core engine functionality, a graphical editor, runtime, utilities, and libraries like Jolt Physics.

## Table of Contents
- [Introduction](#introduction)
- [Table of Contents](#table-of-contents)
- [Installation](#installation)
- [Usage](#usage)
- [Packaging a Release Distribution](#packaging-a-release-distribution)
- [Features](#features)
- [Project Structure](#project-structure)
- [Dependencies](#dependencies)
- [Configuration](#configuration)
- [Contributors](#contributors)
- [License](#license)

## Installation

> **Platform:** Windows only.

### Prerequisites
- **Visual Studio 2026** with the C++ workload (the dependency builder uses the `Visual Studio 18 2026` generator and the `v145` toolset).
- **Vulkan SDK** installed, with the `VULKAN_SDK` environment variable set to its installation path.
- **CMake** on your `PATH` (used by `build_dependencies.bat` to build Assimp, OpenAL, FreeType, libogg, and libvorbis).
- **Premake5** on your `PATH` (used by `premake.bat` to generate the Visual Studio solution).
- *(Only required to package a release)* **Apache Maven** and **JDK 25** on your `PATH` — used to build the Java launcher app-image. See [Packaging a Release Distribution](#packaging-a-release-distribution).

### Steps
1. Clone the repository with submodules:
    ```bash
    git clone --recurse-submodules https://github.com/matan45/VertexForge.git
    ```
2. Download the Streamline release zip from [NVIDIA-RTX/Streamline](https://github.com/NVIDIA-RTX/Streamline).
3. Create a `streamline` folder under `dependencies/` and extract the release into it so it contains the `bin`, `include`, and `lib` folders:
    ```
    dependencies/streamline/bin
    dependencies/streamline/include
    dependencies/streamline/lib
    ```
4. Build the CMake-based dependencies:
    ```bash
    build_dependencies.bat
    ```
5. Generate the Visual Studio solution:
    ```bash
    premake.bat
    ```
6. Open the generated solution (`VFEngine/VertexForge.sln`) in your IDE and build the projects in `Debug` or `Release` mode.

## Usage
The primary entry points for development are the `Editor` and `Runtime` projects:
- **Editor**: Launches the graphical editor interface.
- **Runtime**: Runs the application using the built engine.

To run the engine, simply build and run the `Editor` or `Runtime` projects.

### Running in Debug/Release Mode
To run in `Debug` or `Release` configurations:
1. Select the desired configuration in your IDE (e.g., `Debug` or `Release`).
2. Build the solution.
3. Execute the `Editor` or `Runtime` project from the `bin` directory.

## Packaging a Release Distribution

To produce a single, ready-to-ship folder containing everything needed to distribute the engine — the **Editor**, **Runtime**, plugin **SDK**, **resources**, **Tests**, and the **Java launcher** (as a self-contained app-image with a bundled JRE) — use the `package-release` premake action.

> This packages the *engine/editor distribution*. It is different from the in-editor **Game Export**, which packages an individual game into a `.vfpak`.

### Prerequisites
- The Release binaries must already be built.
- **Apache Maven** and **JDK 25** on your `PATH` (to build the launcher app-image). If either is missing, the native bundle is still produced and the launcher step is skipped with a warning.

### Steps
1. Build the engine in Release:
    ```bash
    msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64
    ```
2. Assemble the distribution:
    ```bash
    premake5 package-release
    ```

This writes everything under `dist/VertexForge/`:

```
dist/VertexForge/
├── VertexForge Launcher/           # self-contained Java launcher app-image (bundled JRE) — entry point
├── bin/
│   ├── Editor/Release/x64/         # Editor.exe + all DLLs
│   ├── Editor/resources/           # editor/, shaders/, ibl/  (resolved as ../../resources/ by Editor.exe)
│   ├── Runtime/Release/x64/        # Runtime.exe + all DLLs
│   ├── Runtime/resources/          # editor/, shaders/, ibl/  (resolved as ../../resources/ by Runtime.exe)
│   └── Tests/Release/x64/          # Tests.exe + DLLs
└── sdk/                            # plugin headers, deps, lib/Release/imgui.lib, project template
```

Run `dist/VertexForge/VertexForge Launcher/VertexForge Launcher.exe` to start the launcher, which in turn launches the bundled Editor. End users of the distribution do **not** need Java installed.

## Features
- **Modular Engine Architecture**: Split into `Core`, `Graphics`, `Runtime`, and `Utilities` projects for flexible development.
- **Vulkan-based Rendering**: Uses Vulkan for high-performance graphics rendering.
- **ImGui Integration**: Provides a graphical interface for development tools and editors.
- **Physics Engine**: Integrates Jolt Physics for real-time simulation.
- **Cross-Platform Compatibility**: Supports Windows with future plans for other platforms.

## Project Structure
- **VFEngine**
  - `editor`: Graphical editor application.
  - `core`: Core engine functionality.
  - `graphics`: Rendering and graphics pipeline.
  - `runtime`: Main application runtime.
  - `utilities`: Common utilities shared across projects.
  - `dependencies`: External libraries (GLFW, ImGui, Jolt, etc.).
  
## Dependencies
- **Vulkan SDK**: For rendering.
- **GLFW**: For window management and input.
- **ImGui**: For GUI rendering.
- **spdlog**: For logging.
- **Jolt Physics**: Physics simulation library.

### Library Dependencies
- GLFW
- spdLog
- ImGui
- JoltPhysics
- glm
- stb

## Configuration
### Build Configurations
- **Debug**: Unoptimized with debug symbols. Defines `DEBUG`.
- **Development**: Optimized *and* keeps debug symbols — for profiling/debugging a fast build. Defines `NDEBUG` and `VF_DEVELOPMENT`.
- **Release**: Optimized for performance, no debug symbols. Defines `NDEBUG`.

### Environment Variables
Ensure the following environment variable is set:
- `VULKAN_SDK`: Path to your Vulkan SDK installation.

## Contributors
- [Matatn Migdal](https://github.com/matan45) (Lead Developer)


## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
