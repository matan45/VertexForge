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
  - **An OpenJDK build of 25.0.2 or newer**, for example [Temurin 25.0.5](https://adoptium.net/), or Oracle JDK 25.0.3 or newer. JDK 25.0.0 and 25.0.1 are refused: their jpackage leaves the `.vfproj` association out of an MSI built from an app image ([JDK-8372753](https://bugs.openjdk.org/browse/JDK-8372753), fixed in OpenJDK 25.0.2 and Oracle JDK 25.0.3). Oracle JDK 25.0.2 only gets a warning; the check of the finished MSI tells whether its association is there. The installer is built with the jpackage of the JDK Maven uses — `JAVA_HOME`, else the `java` on your `PATH` — because jpackage only packages app images its own version built. Oracle's installer puts only a `java.exe` stub on `PATH` (`...\Common Files\Oracle\Java\javapath`), so set `JAVA_HOME` to the JDK you want. To install Temurin with winget:
    ```bat
    winget install --id EclipseAdoptium.Temurin.25.JDK -e
    rem then, in the terminal that runs package-release (use the folder the installer created):
    set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-25.0.<n>-hotspot"
    set "PATH=%JAVA_HOME%\bin;%PATH%"
    ```
  - The **WiX Toolset 3.14.1**: `winget install --id WiXToolset.WiXToolset -e`, or [wix314.exe](https://github.com/wixtoolset/wix3/releases/tag/wix3141rtm). It installs into `C:\Program Files (x86)\WiX Toolset v3.14\bin`, where jpackage finds it without a `PATH` change. WiX is only the tool jpackage builds the `.msi` with: it is needed on the machine that packages, never by the people who install. If its installer asks for .NET Framework 3.5, allow it. jpackage also takes WiX 4 or newer (`wix.exe` with the `WixToolset.Util.wixext` extension). It uses a toolset on your `PATH` first, WiX 4+ before WiX 3; only when `PATH` has none does it take the newest one from `%USERPROFILE%\.dotnet\tools` or `WiX Toolset v*\bin` under the Program Files folders. Open a new terminal after installing.

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
    # signed (see "Signing" below), with a certificate from the Windows certificate store:
    premake5 package-release --sign-sha1=<thumbprint>
    # or with Azure Artifact Signing:
    premake5 package-release --sign-dlib=<path>\Azure.CodeSigning.Dlib.dll --sign-dmdf=<path>\metadata.json
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

Run `dist/VertexForge/VertexForge Launcher.exe` to start the launcher, which in turn launches the bundled Editor (with `--skip-launcher`, run `dist/VertexForge/bin/Editor/Release/x64/Editor.exe`). End users of the distribution do **not** need Java installed. The command ends with a summary of what was copied. What the dev tree's Editor, Runtime and Tests write into their exe folders while they run (`logs/`, `crashes/`, `cache/`, `saves/`, `imgui.ini`) and the linker's `.pdb`/`.ilk` files are never shipped; the summary lists what was left out. The packaged editor writes none of these into its own folder: its `engine.json` says `"stateLocation": "user"`, so it keeps them in `%USERPROFILE%\.vertexforge\editor` (VK-1732).

### Installer (MSI)

`dist/VertexForge-<VERSION>.msi` installs exactly the `dist/VertexForge/` folder:

- **For the current user only, with no UAC prompt**, into `%LOCALAPPDATA%\VertexForge Engine`. It shows up in *Apps & features* as *VertexForge Launcher*. The launcher exe runs in the UTF-8 code page (like the Editor), so it works under any user name and opens projects from any folder.
- A **Start menu** entry *VertexForge > VertexForge Launcher*.
- **Double-clicking a `.vfproj`** opens it in the launcher (`"…\VertexForge Launcher.exe" "%1" %*`): the launcher adds the project to its list and opens it, or hands it to the launcher that is already running, which comes to the front.
- **Upgrades in place:** installing a newer version replaces the installed one in the same folder. The upgrade code is permanent (`4e0f9be5-20f3-4617-bafd-1e0a49993c67` in `tools/package_release.lua`, together with the install folder); never change it, or newer installers stop replacing older ones. An older installer silently replaces a newer install (downgrade). An installer of the version that is already installed, but built again, is refused (MSI error 1638), so bump `VERSION` for every release and uninstall first when testing a rebuild.
- **Uninstalling and upgrading keep your data:** `~/.vertexforge` (launcher settings, recent projects, editor preferences, layouts and launch logs), `Documents\VertexForge\Plugins` and every project folder outside the install folder stay.
- **Uninstalling AND upgrading delete the whole install folder**, so the installed editor keeps nothing of yours there (VK-1732). Its logs, crash reports (`crashes/`, which the launcher shows after a crash), window layout (`imgui.ini`), plugin data (`plugin-data/<Plugin>/`) and Plugin Manager choices (`plugin-overrides.json`, on top of the shipped plugins' defaults) are in `%USERPROFILE%\.vertexforge\editor`; a plugin created with *Plugin Manager > New Plugin…* goes in `Documents\VertexForge\Plugins\<Name>`, which the editor also loads plugins from (build it with premake5 and Visual Studio: the dialog lists the steps). The first upgrade *from* a version older than VK-1732 still loses what that version had written into its install folder.
- Close the launcher and every editor before installing, upgrading or uninstalling; files in use make Windows ask for a restart.
- **To uninstall**, use *Settings > Apps > Installed apps > VertexForge Launcher*, or double-click `Uninstall VertexForge.cmd` in the install folder (it runs `bin\Uninstall.ps1`, which starts the same Windows Installer uninstall for the copy installed in that folder, and refuses in any other folder, such as `dist\VertexForge`). Do not just delete the install folder: Windows still lists the product as installed, and the same version's MSI then no longer installs (uninstall it from *Installed apps* first; it works without the folder).
- An installer built without signing options is unsigned, and Windows SmartScreen warns about a downloaded copy ("Unknown publisher"). A signed one names its publisher, but SmartScreen may still warn until the certificate has built up reputation: since 2024 that applies to EV certificates too.
- The installed launcher and one started from `dist/VertexForge/` share the same `~/.vertexforge`, so a double-click goes to whichever of them is running.

Before the package is swapped in, `tools/msi_inspect.ps1` reads the MSI's own tables, and `package-release` checks the version, the upgrade code, the per-user marking, the install folder, every installed file, the Start menu entry and the `.vfproj` association. You can run it on any MSI: `powershell -ExecutionPolicy Bypass -File tools\msi_inspect.ps1 -Msi <file.msi> -Out <facts.json>`.

### Signing

`--sign-sha1=<thumbprint>` signs every exe and DLL of the package, then the installer, with a certificate from `Cert:\CurrentUser\My` (`--sign-machine-store` for `Cert:\LocalMachine\My`): an OV or EV certificate on its hardware token or behind its vendor's cloud key provider. `--sign-dlib=<x64 Azure.CodeSigning.Dlib.dll> --sign-dmdf=<metadata.json>` signs with Azure Artifact Signing instead (its `metadata.json` names `Endpoint`, `CodeSigningAccountName` and `CertificateProfileName`). Every signature is SHA-256 with an RFC 3161 timestamp, from `http://timestamp.digicert.com` (or `http://timestamp.acs.microsoft.com` with Artifact Signing) unless `--sign-timestamp=<url>` names another. `signtool` comes from the newest Windows SDK, or `--signtool=<path>`.

- The checks run before anything is written: the certificate must be present with its private key, allow code signing, be valid now and chain to a trusted root.
- Only the staged copies are signed, after they were verified, so the build output in `bin/` stays unsigned.
- Files that already carry a valid signature of their own (NVIDIA's DLSS/Streamline DLLs, LunarG's shaderc, Microsoft's and Oracle's runtime DLLs) are left exactly as they are: Streamline refuses a re-signed plugin.
- Afterwards `signtool verify /pa` must pass for every exe, DLL and the installer, and the installer must contain the signed files.
- The exes' version resources (product, company and version from `VERSION`) are checked too, signed or not.
- `tools/authenticode_inspect.ps1 -Root <folder>` (or `-File <file>`, `-Certificate <thumbprint>`) prints the same facts as JSON.

To try signing without a real certificate, make a test one and trust it for your account only:

```powershell
$c = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=VertexForge Test Signing" -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256
Export-Certificate -Cert $c -FilePath $env:TEMP\vf-test.cer
Import-Certificate -FilePath $env:TEMP\vf-test.cer -CertStoreLocation Cert:\CurrentUser\Root   # Windows asks to confirm
premake5 package-release --sign-sha1=$($c.Thumbprint)
```

Remove it again with `Remove-Item Cert:\CurrentUser\Root\<thumbprint>` and `Remove-Item Cert:\CurrentUser\My\<thumbprint>`, and never hand out a package signed with it: on other machines its signatures do not verify.

An exported game's exe is a copy of `Runtime.exe`: the exporter removes the engine's signature from it and gives it the game's own name, version and publisher, so the game's developer signs it with their own certificate. The engine DLLs shipped with the game keep their signatures.

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
