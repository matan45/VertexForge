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
- *(Only required to package a release)* **JDK 25** on your `PATH` or in `JAVA_HOME` — used to build the Java launcher app-image, together with the Windows SDK's `mt.exe` (installed with Visual Studio's C++ workload). Maven itself comes from the launcher's Maven wrapper (`launcher/mvnw.cmd`). The MSI installer also needs JDK **25.0.2 or newer** and the **WiX Toolset 3.14.1**. See [Packaging a Release Distribution](#packaging-a-release-distribution).

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

To produce a single, ready-to-ship folder containing everything needed to distribute the engine — the **Editor**, **Runtime**, plugin **SDK**, **resources**, **Tests**, the **plugins**, the engine **content** projects copy from (script library, example scripts, VFX library), and the **Java launcher** (as a self-contained app-image with a bundled JRE) — and its per-user **MSI installer**, use the `package-release` premake action.

> This packages the *engine/editor distribution*. It is different from the in-editor **Game Export**, which packages an individual game into a `.vfpak`.

### Prerequisites
- The Release binaries must already be built, plugins included (the package ships each plugin's Release DLL from `bin/<Name>/Release/x64/`, never the copy of the last-built configuration in `plugins/<Name>/`).
- **JDK 25** on your `PATH` or in `JAVA_HOME` (to build the launcher app-image). The Maven wrapper (`launcher/mvnw.cmd`) downloads the pinned Maven version on first use, and the build runs the launcher's unit tests. On first use it also downloads the JavaFX jmods (about 43 MiB, checksum-verified, then cached under `~/.m2/repository/.cache/vertexforge`), so that run needs network access. If the JDK is missing or a launcher test fails, packaging fails; pass `--skip-launcher` to package without the launcher.
- For the installer (skip it with `--skip-msi`):
  - **An OpenJDK build of 25.0.2 or newer**, for example [Temurin 25.0.5](https://adoptium.net/), or Oracle JDK 25.0.3 or newer. JDK 25.0.0 and 25.0.1 are refused: their jpackage leaves the `.vfproj` association out of an MSI built from an app image ([JDK-8372753](https://bugs.openjdk.org/browse/JDK-8372753), fixed in OpenJDK 25.0.2 and Oracle JDK 25.0.3). Oracle JDK 25.0.2 only gets a warning; the check of the finished MSI tells whether its association is there. The installer is built with the jpackage of the JDK Maven uses — `JAVA_HOME`, else the `java` on your `PATH` — because jpackage only packages app images its own version built. Oracle's installer puts only a `java.exe` stub on `PATH` (`...\Common Files\Oracle\Java\javapath`), so set `JAVA_HOME` to the JDK you want.
  - The **WiX Toolset 3.14.1** ([wix314.exe](https://github.com/wixtoolset/wix3/releases/tag/wix3141rtm); it installs into `C:\Program Files (x86)\WiX Toolset v3.14\bin`, where jpackage finds it). jpackage also takes WiX 4 or newer (`wix.exe` with the `WixToolset.Util.wixext` extension). It uses a toolset on your `PATH` first, WiX 4+ before WiX 3; only when `PATH` has none does it take the newest one from `%USERPROFILE%\.dotnet\tools` or `WiX Toolset v*\bin` under the Program Files folders. Open a new terminal after installing.

Any failure exits non-zero. The package is assembled and verified in `dist/.VertexForge-staging/`, the installer is built from it in `dist/.VertexForge-installer/` and checked, and only then is the package swapped in, so a failed run leaves an existing `dist/VertexForge/` as it was; if the swap itself cannot complete, the error says where the previous package is. Close any Editor, Runtime or launcher running from `dist/VertexForge/` first. The Release build must match the current sources (`bin/Editor/Release/x64/engine.json` is compared with them), so re-run `premake5 vs2022` and rebuild Release after changing `VERSION` or the plugin API.

### Steps
1. Build the engine in Release:
    ```bash
    msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64
    ```
2. Assemble the distribution:
    ```bash
    premake5 package-release
    # or, without WiX (ships the folder, no installer):
    premake5 package-release --skip-msi
    # or, without a JDK (ships no launcher, so no installer either):
    premake5 package-release --skip-launcher
    ```

This writes everything under `dist/VertexForge/`, and the installer beside it. The folder is the launcher's own app image, so its root holds the launcher next to the engine:

```
dist/VertexForge-<VERSION>.msi      # the per-user installer (not with --skip-msi or --skip-launcher)
dist/VertexForge/
├── VertexForge Launcher.exe        # the Java launcher — entry point
├── app/, runtime/                  # the launcher's module and its own bundled JRE
├── bin/
│   ├── Editor/Release/x64/         # Editor.exe + all DLLs
│   ├── Editor/resources/           # editor/, shaders/, ibl/  (resolved as ../../resources/ by Editor.exe)
│   ├── Runtime/Release/x64/        # Runtime.exe + all DLLs
│   ├── Runtime/resources/          # editor/, shaders/, ibl/  (resolved as ../../resources/ by Runtime.exe)
│   └── Tests/Release/x64/          # Tests.exe + DLLs
├── sdk/                            # plugin headers, deps, lib/Release/imgui.lib, project template
├── plugins/<Name>/                 # <Name>.vfplugin + the Release <Name>.dll (+ the plugin's assets/, resources/), found by the Editor four levels up
└── assets/
    ├── scripts/lib/                # the mType script library (byte-identical to the repo's)
    ├── scripts/game/examples/      # the example scripts
    ├── vfx/library/                # the VFX library projects copy into assets/vfx/library
    └── templates/                  # the project templates new projects are made from (VK-1689)
```

Run `dist/VertexForge/VertexForge Launcher.exe` to start the launcher, which in turn launches the bundled Editor (with `--skip-launcher`, run `dist/VertexForge/bin/Editor/Release/x64/Editor.exe`). End users of the distribution do **not** need Java installed. The command ends with a summary of what was copied. What the Editor, Runtime and Tests write into their exe folders while they run (`logs/`, `crashes/`, `cache/`, `saves/`, `imgui.ini`) and the linker's `.pdb`/`.ilk` files are never shipped; the summary lists what was left out.

### Installer (MSI)

`dist/VertexForge-<VERSION>.msi` installs exactly the `dist/VertexForge/` folder:

- **For the current user only, with no UAC prompt**, into `%LOCALAPPDATA%\Programs\VertexForge`. It shows up in *Apps & features* as *VertexForge Launcher*. The launcher exe runs in the UTF-8 code page (like the Editor), so it works under any user name and opens projects from any folder.
- A **Start menu** entry *VertexForge > VertexForge Launcher*.
- **Double-clicking a `.vfproj`** opens it in the launcher (`"…\VertexForge Launcher.exe" "%1" %*`): the launcher adds the project to its list and opens it, or hands it to the launcher that is already running, which comes to the front.
- **Upgrades in place:** installing a newer version replaces the installed one in the same folder. The upgrade code is permanent (`4e0f9be5-20f3-4617-bafd-1e0a49993c67` in `tools/package_release.lua`, together with the install folder); never change it, or newer installers stop replacing older ones. An older installer silently replaces a newer install (downgrade). An installer of the version that is already installed, but built again, is refused (MSI error 1638), so bump `VERSION` for every release and uninstall first when testing a rebuild.
- **Uninstalling keeps your data:** `~/.vertexforge` (launcher settings, recent projects, editor preferences and logs) and every project folder outside the install folder stay.
- **Uninstalling AND upgrading delete the whole install folder**, including what the editor wrote there: `logs/`, `crashes/`, the Runtime's `cache/`, the window layout (`imgui.ini`), plugin data (`plugins/data/`), the Plugin Manager's enabled choices, and any plugin created with *New Plugin…* (it is scaffolded under `plugins/`). Keep nothing of your own in the install folder (VK-1732 moves this editor state to a per-user folder).
- Close the launcher and every editor before installing, upgrading or uninstalling; files in use make Windows ask for a restart.
- The installer is not signed yet, so Windows SmartScreen may warn about a downloaded copy.
- The installed launcher and one started from `dist/VertexForge/` share the same `~/.vertexforge`, so a double-click goes to whichever of them is running.

Before the package is swapped in, `tools/msi_inspect.ps1` reads the MSI's own tables, and `package-release` checks the version, the upgrade code, the per-user marking, the install folder, every installed file, the Start menu entry and the `.vfproj` association. You can run it on any MSI: `powershell -ExecutionPolicy Bypass -File tools\msi_inspect.ps1 -Msi <file.msi> -Out <facts.json>`.

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
