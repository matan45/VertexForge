workspace "VertexForge"
   configurations { "Debug", "Development", "Release" }
   platforms { "x64" }
   location "VFEngine"  -- Specify where to place generated files
   startproject "Editor"  -- Set the default startup project

   -- Target latest Windows SDK and VS2026 toolset (v145) to avoid retargeting dialog
   filter "system:windows"
      systemversion "latest"
      toolset "v145"
   filter {}

   -- Enable UTF-8 support for all C++ projects (required by spdlog/fmt)
   -- /bigobj: some TUs (e.g. HierarchyService.cpp, Tests) instantiate large template folds
   -- over the full component inventory and exceed the default COFF section limit.
   filter "language:C++"
      buildoptions { "/utf-8", "/MP", "/bigobj" }
      defines {
         "VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1",
         "GLM_FORCE_DEPTH_ZERO_TO_ONE"  -- Vulkan uses [0,1] depth range, not OpenGL's [-1,1]
      }
   filter {}

-- Check if the Vulkan SDK environment variable is set
local vulkanLibPath = os.getenv("VULKAN_SDK")
if not vulkanLibPath then
   error("VULKAN_SDK environment variable is not set.")
end

-- Applies the standard per-config compile settings to the current project.
-- Development = optimized like Release (NDEBUG, /O2) but keeps debug symbols
-- and a VF_DEVELOPMENT define for editor-only niceties. Vulkan validation
-- layers and Jolt asserts stay OFF (NDEBUG path), giving a fast day-to-day
-- play-test build. Debug-only extras (JPH_ENABLE_ASSERTS, VF_ENABLE_VALIDATION)
-- are passed via extraDebugDefines.
function vfStandardConfigs(extraDebugDefines)
   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"
   if extraDebugDefines then
      filter "configurations:Debug"
         defines(extraDebugDefines)
   end
   filter "configurations:Development"
      defines { "NDEBUG", "VF_DEVELOPMENT" }
      optimize "On"
      symbols "On"
   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
   filter {}
end

-- Group for Engine Projects
group "Engine"

-- Project 1: Editor (ImGui-based editor application)
-- Editor accesses engine through Services APIs and EditorBootstrap
project "Editor"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/editor"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/editor/**.hpp", "VFEngine/editor/**.cpp", "VFEngine/editor/app.rc", "resources/editor/**.vfImage" }

   includedirs {
	  "dependencies/imgui",
	  "dependencies/ImGuizmo/src",
	  "dependencies/imgui-node-editor",
	  "dependencies/spdlog/include",
	  "dependencies/glm",
	  "dependencies/entt/single_include",
	  "dependencies/json/single_include",
	  "VFEngine/utilities",
	  "VFEngine/core/bootstrap",          -- For EditorBootstrap
	  "VFEngine/core/controllers",        -- For ImguiWindow base class
	  "dependencies/IconFontCppHeaders",
	  "VFEngine/import/controllers",
	  "VFEngine/import/types",            -- For MeshSocketWriter, AnimationEventIO
	  "VFEngine/services",                -- Services layer interfaces
	  "VFEngine/plugin",                  -- Plugin system
	  "dependencies/mType/mType",         -- mType plugin C ABI (PluginContext.hpp -> plugin/PluginHostApi.h)
	  "VFEngine/utilities/procedural",    -- Procedural heightmap generation
	  "VFEngine/utilities/imageprocessing" -- Image background removal
   }

   links {
      "Core",                           -- Link Core project
	  "Import",
	  "Services",                       -- Link Services project
	  "Plugin",                         -- Plugin system
	  "imgui",                          -- For imgui-node-editor in ShaderGraphEditor
	  "ProceduralGen",                  -- Procedural heightmap generation
	  "ImageProcessing",                -- Image background removal
	  "GameExport",                     -- Game export pipeline with shader pre-compilation
	  "ECSRegistry",                    -- Shared ECS registry singleton DLL
	  "AssetDB"                         -- Shared asset database singleton DLL
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   -- Windows-specific libraries for splash screen and mType net/plugin
   filter "system:windows"
      links { "gdiplus", "winhttp", "ws2_32" }
      linkoptions { "/ignore:4006" }
   filter {}

   vfStandardConfigs()

   -- OpenAL DLL: CMake-built per-config — Debug build uses the debug CRT DLL,
   -- Development/Release use the release one
   filter "configurations:Debug"
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Debug/OpenAL32.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
      }
   filter "configurations:Development or Release"
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Release/OpenAL32.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
      }

   -- Streamline development DLLs (no App ID required) for Debug + Development
   filter "configurations:Debug or Development"
      postbuildcommands {
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.interposer.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.common.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.pcl.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.dlss.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/nvngx_dlss.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.dlss_g.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/nvngx_dlssg.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.directsr.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.reflex.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/NvLowLatencyVk.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
      }

   -- Streamline production DLLs (requires NVIDIA App ID for shipping)
   -- NOTE: Editor stays a ConsoleApp in Release too — console logs visible in
   -- every config (user preference). The exported game is built by GameExport
   -- from Runtime, so this does not affect shipped games.
   filter "configurations:Release"
      postbuildcommands {
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.interposer.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.common.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.pcl.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.dlss.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/nvngx_dlss.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.dlss_g.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/nvngx_dlssg.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.directsr.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.reflex.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/NvLowLatencyVk.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
      }
   filter {}

-- Project 2: Core
project "Core"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/core"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/core/**.hpp", "VFEngine/core/**.cpp" }

   -- Extracted subsystems compiled by their own projects
   removefiles {
      "VFEngine/core/audio/**",
      "VFEngine/core/physics/**"
   }

   includedirs {
      "VFEngine/graphics/controllers",   -- Graphics headers
      "VFEngine/window/controllers",     -- Window headers
	  "dependencies/spdlog/include",
	  "dependencies/entt/single_include",
      "VFEngine/utilities",              -- Utilities headers
	  "VFEngine/services",               -- Services layer interfaces
	  "dependencies/imgui",
	  "dependencies/ImGuizmo/src",
	  "dependencies/glm",
	  "dependencies/glfw/include",
	  "dependencies/imgui/backends",
	  "dependencies/openal-soft/include", -- OpenAL headers
	  "dependencies/mtype/mType",         -- mType scripting language
	  vulkanLibPath.."/Include",
	  "dependencies/json/single_include",  -- nlohmann/json (for PluginComponents)
	  "dependencies/JoltPhysics",          -- Jolt Physics headers
	  "dependencies/recastnavigation/Recast/Include",   -- Recast navmesh generation
	  "dependencies/recastnavigation/Detour/Include",   -- Detour pathfinding
	  "dependencies/recastnavigation/DetourCrowd/Include" -- DetourCrowd agent steering
   }

   links { "Graphics", "Audio", "Physics", "Animation", "mType", "jolt", "recast" }
   defines { "_CRT_SECURE_NO_WARNINGS", "JPH_OBJECT_STREAM", "JPH_SHARED_LIBRARY" }

   vfStandardConfigs({ "JPH_ENABLE_ASSERTS" })


project "Import"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/import"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/import/**.hpp", "VFEngine/import/**.cpp" }

   includedirs {
	  "dependencies/spdlog/include",
      "VFEngine/utilities",             -- Utilities headers
      "dependencies/stb",               -- stb headers
      "dependencies/dr_libs",           -- dr_mp3.h, dr_wav.h, and other dr_libs headers
      "dependencies/tinyexr",           -- exr headers
      "dependencies/assimp/include",           -- Assimp headers
      "dependencies/assimp/build/include",     -- Assimp generated headers (config.h)
	  "dependencies/glm",
	  "dependencies/meshoptimizer/src",  -- meshoptimizer for LOD generation
	  "dependencies/v-hacd",             -- V-HACD for convex decomposition
	  "dependencies/freetype/include",   -- FreeType headers
	  "dependencies/ispc_texcomp",       -- ISPCTextureCompressor (BC7/BC6H)
	  "dependencies/bcdec",              -- BC7/BC6H block decompression
	  "dependencies/libogg/include",       -- Ogg container format (for Vorbis encoding)
	  "dependencies/libogg/build/include", -- Ogg generated config headers
	  "dependencies/libvorbis/include"   -- Vorbis audio compression (encoding at import)
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_IMPORT_BUILD_DLL", "MESHOPTIMIZER_API=__declspec(dllimport)" }

   links { "Utilities", "Destruction", "meshoptimizer", "ispc_texcomp", "AssetDB" }

   -- Copy DLLs to Editor output directory (Import is Editor-only)
   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/Import/%{cfg.buildcfg}/x64/Import.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()

   -- CMake-built libs: Debug links the /MDd debug variants; Development and
   -- Release link the /MD release variants (mixing CRTs is a link error)
   filter "configurations:Debug"
      libdirs {
         "dependencies/assimp/build/lib/Debug",
         "dependencies/freetype/build/Debug",
         "dependencies/libogg/build/Debug",
         "dependencies/libvorbis/build/lib/Debug"
      }
      links { "assimp-vc145-mtd.lib", "freetyped.lib", "ogg.lib", "vorbis.lib", "vorbisenc.lib" }
      postbuildcommands {
         "{COPY} ../../dependencies/assimp/build/bin/Debug/assimp-vc145-mtd.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
      }

   filter "configurations:Development or Release"
      libdirs {
         "dependencies/assimp/build/lib/Release",
         "dependencies/freetype/build/Release",
         "dependencies/libogg/build/Release",
         "dependencies/libvorbis/build/lib/Release"
      }
      links { "assimp-vc145-mt.lib", "freetype.lib", "ogg.lib", "vorbis.lib", "vorbisenc.lib" }
      postbuildcommands {
         "{COPY} ../../dependencies/assimp/build/bin/Release/assimp-vc145-mt.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
      }
   filter {}


-- Project 3: Graphics
project "Graphics"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/graphics"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/graphics/**.hpp", "VFEngine/graphics/**.cpp" ,"resources/shaders/**.glsl"}

   -- Extracted subsystems compiled by their own projects
   removefiles {
      "VFEngine/graphics/animation/**",
      "VFEngine/graphics/render/vfx/**"
   }
   -- AnimationComputePipeline stays in Graphics (depends on Vulkan core utilities)
   files {
      "VFEngine/graphics/animation/AnimationComputePipeline.hpp",
      "VFEngine/graphics/animation/AnimationComputePipeline.cpp",
      "VFEngine/graphics/animation/AnimationGPUData.hpp"
   }

   includedirs {
      "dependencies/glfw/include",
      "dependencies/spdlog/include",
	  "dependencies/imgui",
	  "dependencies/imgui/backends",
      "dependencies/glm",
	  "dependencies/entt/single_include",
      "dependencies/stb",
      "dependencies/json/single_include",
      "VFEngine/utilities",
      "VFEngine/window/controllers",
	  "dependencies/IconFontCppHeaders",
      "dependencies/ispc_texcomp",
      "dependencies/streamline/include",
      vulkanLibPath.."/Include"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_STREAMLINE_ENABLED" }

   libdirs {
      vulkanLibPath.."/Lib",
      "dependencies/streamline/lib/x64"
   }

   links {
      "Window",
	  "VFX",
	  "imgui",
	  "ispc_texcomp",
	  "Memory",
	  "sl.interposer.lib",
	  "shaderc_shared.lib"
   }

   -- Suppress LNK4006: __NULL_IMPORT_DESCRIPTOR collision between sl.interposer.lib and shaderc_shared.lib
   linkoptions { "/ignore:4006" }

   -- VF_ENABLE_VALIDATION: Vulkan validation layers + debug messenger.
   -- Enabled in Debug and Development (user preference: keep validation while
   -- play-testing); Release stays validation-free. Remove the Development
   -- filter below for a max-speed Development build.
   vfStandardConfigs({ "VF_ENABLE_VALIDATION" })
   filter "configurations:Development"
      defines { "VF_ENABLE_VALIDATION" }
   filter {}

-- Project 4: Runtime (Standalone game runtime - NO Editor/Import dependencies)
project "Runtime"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/runtime"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/runtime/**.hpp", "VFEngine/runtime/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",  -- nlohmann/json (for PluginComponents)
      "VFEngine/utilities",
      "VFEngine/services",              -- Services interfaces only
      "VFEngine/core/bootstrap",        -- For RuntimeBootstrap
      "VFEngine/plugin",                -- Plugin system
      "dependencies/mType/mType"        -- mType plugin C ABI (PluginContext.hpp -> plugin/PluginHostApi.h)
      -- NOTE: NO VFEngine/core/controllers, NO VFEngine/graphics/controllers
   }

   links { "Services", "Core", "Plugin", "ECSRegistry", "AssetDB" }  -- Core linked for RuntimeBootstrap, not direct access

   -- Delay-load shaderc: exported builds ship pre-compiled SPIR-V,
   -- so shaderc_shared.dll is not needed and never loaded at runtime
   linkoptions { "/DELAYLOAD:shaderc_shared.dll" }
   links { "delayimp" }

   filter "system:windows"
      links { "winhttp", "ws2_32" }
      linkoptions { "/ignore:4006" }
   filter {}

   defines { "DOCTEST_CONFIG_DISABLE" }

   vfStandardConfigs()

   -- Copy OpenAL DLL to Runtime output directory (CMake-built per-config)
   filter "configurations:Debug"
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Debug/OpenAL32.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
      }
   filter "configurations:Development or Release"
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Release/OpenAL32.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
      }

   -- Streamline runtime DLLs so DLSS / Frame Generation / Reflex work in standalone
   -- Runtime.exe (not only the Editor). Missing DLLs are handled gracefully (slInit
   -- fails -> streamlineAvailable=false -> upscaling/Reflex no-op).
   filter "configurations:Debug or Development or Release"
      postbuildcommands {
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.interposer.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.common.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.pcl.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.dlss.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/nvngx_dlss.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.dlss_g.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/nvngx_dlssg.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.directsr.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.reflex.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/",
         "{COPY} ../../dependencies/streamline/bin/x64/development/NvLowLatencyVk.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
      }
   filter {}

-- Project 5: Utilities (Moved before Graphics)
project "Utilities"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/**.hpp", "VFEngine/utilities/**.cpp" }

   -- Extracted subsystems compiled by their own projects
   removefiles {
      "VFEngine/utilities/terrain/**",
      "VFEngine/utilities/serialization/**",
      "VFEngine/utilities/world/WorldDefinitionSerialization.*",
      "VFEngine/utilities/world/WorldSectorSerialization.*",
      "VFEngine/utilities/world/**",
      "VFEngine/utilities/animator/**",
      "VFEngine/utilities/vfx/**",
      "VFEngine/utilities/procedural/**",
      "VFEngine/utilities/imageprocessing/**",
      "VFEngine/utilities/memory/**",
      "VFEngine/utilities/weather/**",
      "VFEngine/utilities/destruction/**",
      "VFEngine/utilities/scene/EntityRegistry.cpp",  -- compiled by ECSRegistry DLL
      "VFEngine/utilities/asset/AssetDatabase.cpp"    -- compiled by AssetDB DLL
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
	  "dependencies/entt/single_include",
	  "dependencies/json/single_include",
	  "dependencies/meshoptimizer/src",  -- meshoptimizer for terrain meshlet generation
	  "dependencies/enkiTS/src",         -- enkiTS task scheduler
	  "dependencies/stb",               -- stb_vorbis for runtime Vorbis decoding
      vulkanLibPath.."/Include",         -- Vulkan SDK for shader binary format types
      "dependencies/lz4/lib"             -- LZ4 compression for .vfpak archives
   }

   links { "spdLog", "meshoptimizer", "enkiTS", "lz4" }

   -- Export pipeline compiled in GameExport subsystem
   -- VFPakWriter is export-only; VFPakReader/Format stay in Utilities for runtime use
   removefiles {
      "VFEngine/utilities/export/**",
      "VFEngine/utilities/archive/VFPakWriter.*"
   }

   defines { "MESHOPTIMIZER_API=__declspec(dllimport)" }

   buildoptions { "/bigobj" }

   vfStandardConfigs()


-- Project: Services (Event System, Service Interfaces, Service Implementations)
-- Services provides the abstraction layer - NO direct Core dependencies
project "Services"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/services"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/services/**.hpp", "VFEngine/services/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/glfw/include",        -- For Window types in InputService
      "dependencies/imgui",
      "dependencies/json/single_include",
      "VFEngine/utilities",
      "VFEngine/window/controllers",      -- For Window types in InputService
      vulkanLibPath.."/Include"
      -- NOTE: NO VFEngine/core/controllers - Services uses provider interfaces
   }

   links { "Utilities", "Destruction", "Terrain", "Serialization", "World", "Window", "Weather" }

   vfStandardConfigs()


-- Project: Plugin (Plugin system infrastructure)
-- Plugin provides the SDK API for external DLL plugins and the loading/lifecycle management
project "Plugin"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/plugin"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/plugin/**.hpp", "VFEngine/plugin/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/imgui",
      vulkanLibPath.."/Include",            -- For vk::CommandBuffer in RenderHookTypes
      "dependencies/json/single_include",  -- nlohmann/json (for plugin component registration)
      "dependencies/mType/mType",          -- value::Value header-inline use only (no mType.lib link)
      "VFEngine/utilities",
      "VFEngine/services",
      "VFEngine/import",                   -- For registry/AssetImporter.hpp
      "VFEngine/import/pipeline",          -- For PipelineStage base class
      "VFEngine/core/controllers"          -- For ImguiWindow base class
   }

   links { "Services", "Utilities" }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()



group "Engine"

-- Project 6: Window
project "Window"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/Window"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/window/**.hpp", "VFEngine/window/**.cpp" }

   includedirs {
      "dependencies/glfw/include",
	  "VFEngine/utilities",
	  "dependencies/glm",
	  "dependencies/spdlog/include",
      vulkanLibPath.."/Include"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   links {  "GLFW",
			"Utilities"}  -- Link against Core and Graphics

   vfStandardConfigs()

-- Group for Extracted Subsystems
group "Subsystems"

-- ECSRegistry subsystem (owns the EnTT registry singleton, SharedLib/DLL)
-- Required by any DLL that accesses EntityRegistry (Audio, Serialization, etc.)
project "ECSRegistry"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "VFEngine/utilities/scene/EntityRegistry.hpp",
      "VFEngine/utilities/scene/EntityRegistry.cpp",
      "VFEngine/utilities/scene/ECSRegistryExport.hpp"
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_ECSREGISTRY_BUILD_DLL" }

   links { "spdLog" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/ECSRegistry/%{cfg.buildcfg}/x64/ECSRegistry.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/ECSRegistry/%{cfg.buildcfg}/x64/ECSRegistry.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- AssetDB subsystem (owns the AssetDatabase singleton, SharedLib/DLL)
-- Required by any DLL that accesses AssetDatabase (Serialization, World,
-- Terrain, Animation, Audio, Import, GameExport) so GUID<->path lookups and
-- the dependency graph resolve to one instance across the process.
project "AssetDB"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "VFEngine/utilities/asset/AssetDatabase.hpp",
      "VFEngine/utilities/asset/AssetDatabase.cpp",
      "VFEngine/utilities/asset/AssetDBExport.hpp"
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/json/single_include",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_ASSETDB_BUILD_DLL" }

   -- Utilities: AssetMetadataSerializer + VFSHelpers used by persistence/meta scan
   links { "Utilities", "spdLog" }
   linkoptions { "/ignore:4217" }  -- LNK4217: Utilities.lib imports symbols defined in this DLL

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/AssetDB/%{cfg.buildcfg}/x64/AssetDB.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/AssetDB/%{cfg.buildcfg}/x64/AssetDB.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- Audio subsystem (extracted from Core, SharedLib/DLL)
project "Audio"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/core"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/core/audio/**.hpp", "VFEngine/core/audio/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "VFEngine/utilities",
      "VFEngine/services",
      "dependencies/openal-soft/include"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_AUDIO_BUILD_DLL" }

   -- Services: EventDispatcher used by AudioSceneUpdater
   -- Animation, Terrain: transitive deps from Utilities.lib (ResourceManager references AnimatorAsset/TerrainMaterialAsset)
   links { "Utilities", "Services", "Animation", "Terrain", "ECSRegistry", "AssetDB" }
   linkoptions { "/ignore:4217" }  -- LNK4217: Utilities.lib imports symbols that are local to this DLL

   links { "OpenAL32.lib" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/Audio/%{cfg.buildcfg}/x64/Audio.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Audio/%{cfg.buildcfg}/x64/Audio.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()

   -- OpenAL import lib: CMake-built per-config (Debug = /MDd, Release = /MD)
   filter "configurations:Debug"
      libdirs { "dependencies/openal-soft/build/Debug" }
   filter "configurations:Development or Release"
      libdirs { "dependencies/openal-soft/build/Release" }
   filter {}


-- Physics subsystem (extracted from Core)
project "Physics"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/core"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/core/physics/**.hpp", "VFEngine/core/physics/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "VFEngine/utilities",
      "VFEngine/services",
      "dependencies/JoltPhysics"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "JPH_OBJECT_STREAM", "JPH_SHARED_LIBRARY" }

   vfStandardConfigs({ "JPH_ENABLE_ASSERTS" })


-- Memory subsystem (extracted from Utilities)
project "Memory"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/memory/**.hpp", "VFEngine/utilities/memory/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()


-- Weather subsystem (extracted from Utilities)
project "Weather"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/weather/**.hpp", "VFEngine/utilities/weather/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/json/single_include",
      "dependencies/entt/single_include",
      "VFEngine/utilities",
      "VFEngine/services"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()


-- Destruction subsystem (extracted from Utilities)
project "Destruction"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/destruction/**.hpp", "VFEngine/utilities/destruction/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/v-hacd",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()


-- Terrain subsystem (extracted from Utilities, SharedLib/DLL)
project "Terrain"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/terrain/**.hpp", "VFEngine/utilities/terrain/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "dependencies/meshoptimizer/src",
      "dependencies/enkiTS/src",
      "dependencies/stb",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "MESHOPTIMIZER_API=__declspec(dllimport)", "VF_TERRAIN_BUILD_DLL" }

   links { "Utilities", "meshoptimizer", "enkiTS", "ECSRegistry", "AssetDB" }

   buildoptions { "/bigobj" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/Terrain/%{cfg.buildcfg}/x64/Terrain.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Terrain/%{cfg.buildcfg}/x64/Terrain.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- ProceduralGen subsystem (extracted from Utilities, SharedLib/DLL)
project "ProceduralGen"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/procedural/**.hpp", "VFEngine/utilities/procedural/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_PROCEDURAL_BUILD_DLL" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/ProceduralGen/%{cfg.buildcfg}/x64/ProceduralGen.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- ImageProcessing subsystem (extracted from Utilities, SharedLib/DLL)
project "ImageProcessing"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/imageprocessing/**.hpp", "VFEngine/utilities/imageprocessing/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_IMAGEPROCESSING_BUILD_DLL" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/ImageProcessing/%{cfg.buildcfg}/x64/ImageProcessing.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- Serialization subsystem (extracted from Utilities, SharedLib/DLL)
project "Serialization"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "VFEngine/utilities/serialization/**.hpp",
      "VFEngine/utilities/serialization/**.cpp",
      "VFEngine/utilities/world/WorldDefinitionSerialization.hpp",
      "VFEngine/utilities/world/WorldDefinitionSerialization.cpp",
      "VFEngine/utilities/world/WorldSectorSerialization.hpp",
      "VFEngine/utilities/world/WorldSectorSerialization.cpp",
      "VFEngine/utilities/world/HLODSerialization.hpp",
      "VFEngine/utilities/world/HLODSerialization.cpp"
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_SERIALIZATION_BUILD_DLL" }

   links { "Utilities", "ECSRegistry", "AssetDB" }

   buildoptions { "/bigobj" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/Serialization/%{cfg.buildcfg}/x64/Serialization.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Serialization/%{cfg.buildcfg}/x64/Serialization.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- DataTypes (header-only, extracted from Services for dependency clarity)
project "DataTypes"
   kind "None"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/services"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/services/data/**.hpp" }

   includedirs {
      "dependencies/glm",
      "dependencies/entt/single_include",
      "VFEngine/utilities",
      vulkanLibPath.."/Include"
   }


-- World subsystem (extracted from Utilities)
project "World"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/world/**.hpp", "VFEngine/utilities/world/**.cpp" }

   -- Serialization files are compiled by the Serialization project
   removefiles {
      "VFEngine/utilities/world/WorldDefinitionSerialization.*",
      "VFEngine/utilities/world/WorldSectorSerialization.*",
      "VFEngine/utilities/world/HLODSerialization.*"
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "dependencies/meshoptimizer/src",
      "VFEngine/utilities"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_WORLD_BUILD_DLL" }

   links { "Utilities", "Terrain", "Serialization", "meshoptimizer", "ECSRegistry", "AssetDB" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/World/%{cfg.buildcfg}/x64/World.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/World/%{cfg.buildcfg}/x64/World.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- Animation subsystem (extracted from Graphics + Utilities, SharedLib/DLL)
project "Animation"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/graphics"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "VFEngine/graphics/animation/**.hpp",
      "VFEngine/graphics/animation/**.cpp",
      "VFEngine/utilities/animator/**.hpp",
      "VFEngine/utilities/animator/**.cpp"
   }

   -- AnimationComputePipeline depends on Vulkan core utilities, compiled by Graphics instead
   -- IKTypes.cpp functions moved to inline in header (breaks Serialization→Animation→Services→Serialization cycle)
   removefiles {
      "VFEngine/graphics/animation/AnimationComputePipeline.cpp",
      "VFEngine/graphics/animation/AnimationGPUData.hpp",
      "VFEngine/utilities/animator/IKTypes.cpp",
      "VFEngine/utilities/animator/AnimatorTypes.cpp"
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "VFEngine/utilities",
      "VFEngine/services",
      vulkanLibPath.."/Include"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_ANIMATION_BUILD_DLL" }

   -- Services: EventDispatcher used by RuntimeAnimatorSystem
   -- Terrain: transitive dep from Utilities.lib (ResourceManager references TerrainMaterialAsset)
   links { "Utilities", "Services", "Terrain", "ECSRegistry", "AssetDB" }
   linkoptions { "/ignore:4217" }  -- LNK4217: Utilities.lib imports symbols that are local to this DLL

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/Animation/%{cfg.buildcfg}/x64/Animation.dll ../../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Animation/%{cfg.buildcfg}/x64/Animation.dll ../../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- VFX subsystem (extracted from Graphics + Utilities)
project "VFX"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/graphics"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "VFEngine/graphics/render/vfx/**.hpp",
      "VFEngine/graphics/render/vfx/**.cpp",
      "VFEngine/utilities/vfx/**.hpp",
      "VFEngine/utilities/vfx/**.cpp"
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "dependencies/imgui",
      "dependencies/stb",
      "VFEngine/utilities",
      "VFEngine/services",
      "VFEngine/graphics",
      "VFEngine/window/controllers",
      vulkanLibPath.."/Include"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   libdirs {
      vulkanLibPath.."/Lib"
   }

   links { "shaderc_shared.lib" }

   vfStandardConfigs()


-- GameExport subsystem (extracted from Utilities - shader pre-compilation and game export pipeline, SharedLib/DLL)
project "GameExport"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "VFEngine/utilities/export/**.hpp",
      "VFEngine/utilities/export/**.cpp",
      "VFEngine/utilities/archive/VFPakWriter.hpp",
      "VFEngine/utilities/archive/VFPakWriter.cpp"
   }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "dependencies/lz4/lib",
      "VFEngine/utilities",
      vulkanLibPath.."/Include"
   }

   libdirs {
      vulkanLibPath.."/Lib"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "VF_GAMEEXPORT_BUILD_DLL" }

   links { "Utilities", "Serialization", "lz4", "shaderc_shared.lib", "AssetDB" }

   postbuildcommands {
      "{MKDIR} ../../bin/Editor/%{cfg.buildcfg}/x64",
      "{COPY} ../../bin/GameExport/%{cfg.buildcfg}/x64/GameExport.dll ../../bin/Editor/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- Tests: doctest unit test runner (CPU-only tests, no rendering dependencies)
project "Tests"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/tests"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/tests/**.hpp", "VFEngine/tests/**.cpp" }

   includedirs {
      "dependencies/doctest/doctest",
      "dependencies/spdlog/include",
      "dependencies/glm",
      "dependencies/entt/single_include",
      "dependencies/json/single_include",
      "dependencies/meshoptimizer/src",
      "dependencies/enkiTS/src",
      "dependencies/stb",
      "dependencies/glfw/include",
      "dependencies/imgui",
      "dependencies/imgui/backends",
      "dependencies/ispc_texcomp",
      "dependencies/IconFontCppHeaders",
      "dependencies/lz4/lib",
      "dependencies/recastnavigation/Recast/Include",
      "dependencies/recastnavigation/Detour/Include",
      "dependencies/recastnavigation/DetourCrowd/Include",
      "dependencies/JoltPhysics",
      "VFEngine/utilities",
      "VFEngine/services",
      "VFEngine/graphics",
      "VFEngine/core",
      "VFEngine/window/controllers",
      "VFEngine/plugin",                   -- header-only PluginScaffolder (VK-1284), no link needed
      "VFEngine/import",                   -- ImporterRegistry tests
      "VFEngine/editor",                   -- header-only content browser type table / query parser tests
      vulkanLibPath.."/Include"
   }

   libdirs {
      vulkanLibPath.."/Lib"
   }

   links {
      "Utilities", "Memory", "Destruction", "Terrain", "World", "Serialization",
      "Animation", "ECSRegistry", "AssetDB", "Services", "Import",
      "Graphics", "Window", "VFX", "imgui", "ispc_texcomp", "GLFW", "GameExport",
      "spdLog", "meshoptimizer", "enkiTS", "lz4", "recast",
      "vulkan-1.lib", "shaderc_shared.lib"
   }

   defines {
      "_CRT_SECURE_NO_WARNINGS",
      "MESHOPTIMIZER_API=__declspec(dllimport)",
      "JPH_OBJECT_STREAM", "JPH_SHARED_LIBRARY"
   }

   buildoptions { "/bigobj" }

   -- doctest specializes std::tuple, forbidden under C++20 [tuple.tuple.general]/1
   disablewarnings { "5285" }

   postbuildcommands {
      "{COPY} ../../bin/ECSRegistry/%{cfg.buildcfg}/x64/ECSRegistry.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/AssetDB/%{cfg.buildcfg}/x64/AssetDB.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Terrain/%{cfg.buildcfg}/x64/Terrain.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Serialization/%{cfg.buildcfg}/x64/Serialization.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/World/%{cfg.buildcfg}/x64/World.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Animation/%{cfg.buildcfg}/x64/Animation.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/meshoptimizer/%{cfg.buildcfg}/x64/meshoptimizer.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/GameExport/%{cfg.buildcfg}/x64/GameExport.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} ../../bin/Import/%{cfg.buildcfg}/x64/Import.dll ../../bin/Tests/%{cfg.buildcfg}/x64/",
      "{COPY} " .. vulkanLibPath .. "/Bin/shaderc_shared.dll ../../bin/Tests/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs({ "JPH_ENABLE_ASSERTS" })

   -- Streamline interposer: development build for Debug/Development, production for Release
   filter "configurations:Debug or Development"
      postbuildcommands {
         "{COPY} ../../dependencies/streamline/bin/x64/development/sl.interposer.dll ../../bin/Tests/%{cfg.buildcfg}/x64/"
      }
   filter "configurations:Release"
      postbuildcommands {
         "{COPY} ../../dependencies/streamline/bin/x64/sl.interposer.dll ../../bin/Tests/%{cfg.buildcfg}/x64/"
      }

   -- Import.dll runtime dependency: assimp CMake build (Debug uses /MDd variant)
   filter "configurations:Debug"
      postbuildcommands {
         "{COPY} ../../dependencies/assimp/build/bin/Debug/assimp-vc145-mtd.dll ../../bin/Tests/%{cfg.buildcfg}/x64/"
      }
   filter "configurations:Development or Release"
      postbuildcommands {
         "{COPY} ../../dependencies/assimp/build/bin/Release/assimp-vc145-mt.dll ../../bin/Tests/%{cfg.buildcfg}/x64/"
      }

   filter {}  -- reset filters before next group


-- Group for Libraries
group "libs"

-- Project: LZ4 (fast compression for .vfpak archives)
project "lz4"
   kind "StaticLib"
   language "C"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/lz4/lib/lz4.h",
      "dependencies/lz4/lib/lz4.c",
      "dependencies/lz4/lib/lz4hc.h",
      "dependencies/lz4/lib/lz4hc.c"
   }

   includedirs { "dependencies/lz4/lib" }

   vfStandardConfigs()

-- Project: GLFW
project "GLFW"
   kind "StaticLib"
   language "C"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/glfw/include/GLFW/**.h",
      "dependencies/glfw/src/**.c"
   }

   includedirs {
      "dependencies/glfw/include"
   }

   defines { "_GLFW_WIN32", "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()

-- Project: spdLog (Moved under libs group)
project "spdLog"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/spdlog/include/spdlog/**.h",
      "dependencies/spdlog/src/**.cpp"
   }

   includedirs {
      "dependencies/spdlog/include"
   }

   defines { "SPDLOG_COMPILED_LIB" }

   vfStandardConfigs()


-- Project: imgui (Moved under libs group)
project "imgui"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   -- Only include core ImGui files and Vulkan backend
   files {
      "dependencies/imgui/*.h",
      "dependencies/imgui/*.cpp",
      "dependencies/imgui/backends/imgui_impl_vulkan.*",  -- Only Vulkan part
      "dependencies/imgui/backends/imgui_impl_glfw.*",  -- Only Vulkan part
      "dependencies/ImGuizmo/src/*.h",
      "dependencies/ImGuizmo/src/*.cpp",
      -- imgui-node-editor v0.9.3 flat structure
      "dependencies/imgui-node-editor/*.h",
      "dependencies/imgui-node-editor/*.cpp",
      "dependencies/imgui-node-editor/*.inl"
   }

   -- Exclude folders: misc and examples
   removefiles {
      "dependencies/imgui/misc/**",
      "dependencies/imgui/examples/**",
      "dependencies/ImGuizmo/example/**",
      "dependencies/ImGuizmo/vcpkg-example/**",
      "dependencies/imgui-node-editor/examples/**",
      "dependencies/imgui-node-editor/external/**"
   }

   includedirs {
      "dependencies/imgui",                       -- Core ImGui headers
      "dependencies/imgui/backends",              -- Vulkan backend headers
      "dependencies/ImGuizmo/src",
      "dependencies/imgui-node-editor",           -- imgui-node-editor v0.9.3 headers
	  "dependencies/glfw/include",
      vulkanLibPath.."/Include"                   -- Vulkan SDK headers
   }
   
   libdirs {
      vulkanLibPath.."/Lib"
   }
   
   links {
      "vulkan-1.lib"
   }

   vfStandardConfigs()

-- Project: JoltPhysics (Moved under libs group)
project "jolt"
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/JoltPhysics/Jolt/**.h",
      "dependencies/JoltPhysics/Jolt/**.cpp"
   }

   includedirs {
      "dependencies/JoltPhysics"
   }

   -- Jolt Physics configuration defines
   defines {
      "JPH_OBJECT_STREAM",          -- Enable object serialization
      "JPH_SHARED_LIBRARY",         -- Enable DLL export/import macros
      "JPH_BUILD_SHARED_LIBRARY"    -- Building the DLL: JPH_EXPORT = __declspec(dllexport)
   }

   postbuildcommands {
      "{MKDIR} ../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../bin/jolt/%{cfg.buildcfg}/x64/jolt.dll ../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../bin/jolt/%{cfg.buildcfg}/x64/jolt.dll ../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs({ "JPH_ENABLE_ASSERTS" })


-- Project: mType (Scripting language interpreter)
project "mType"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/mtype/mType/**.hpp",
      "dependencies/mtype/mType/**.cpp",
      "dependencies/mtype/packagemanager/src/**.hpp",
      "dependencies/mtype/packagemanager/src/**.cpp"
   }

   -- Exclude main entry points and tests (kept for standalone executables upstream)
   removefiles {
      "dependencies/mtype/mType/run/**",
      "dependencies/mtype/mType/tests/**",
      "dependencies/mtype/packagemanager/src/Main.cpp"
   }

   includedirs {
      "dependencies/mtype/mType",
      "dependencies/mtype/packagemanager/src",
      "dependencies/mtype/vendor/asmjit"
   }

   files {
      "dependencies/mtype/vendor/asmjit/asmjit/**.cpp",
      "dependencies/mtype/vendor/asmjit/asmjit/**.h"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "MTYPE_SIMD_ENABLED", "ASMJIT_STATIC" }

   -- Platform-specific SIMD configurations
   filter "system:windows"
      systemversion "latest"
      removefiles {
         "dependencies/mtype/mType/net/CurlHttpClient.hpp",
         "dependencies/mtype/mType/net/CurlHttpClient.cpp",
         "dependencies/mtype/mType/net/PosixSocket.hpp",
         "dependencies/mtype/mType/net/PosixSocket.cpp",
         "dependencies/mtype/mType/plugin/PosixPluginLoader.cpp"
      }

   filter "system:linux or system:macosx"
      removefiles {
         "dependencies/mtype/mType/net/WinHttpClient.hpp",
         "dependencies/mtype/mType/net/WinHttpClient.cpp",
         "dependencies/mtype/mType/net/WinSocket.hpp",
         "dependencies/mtype/mType/net/WinSocket.cpp",
         "dependencies/mtype/mType/net/WinSockInit.hpp",
         "dependencies/mtype/mType/net/WinSockInit.cpp",
         "dependencies/mtype/mType/plugin/WinPluginLoader.cpp"
      }

   filter { "system:windows", "configurations:Development or Release" }
      buildoptions { "/arch:AVX2" }

   vfStandardConfigs()


-- Project: meshoptimizer (Mesh simplification for LOD generation)
project "meshoptimizer"
   kind "SharedLib"
   language "C++"
   cppdialect "C++17"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/meshoptimizer/src/meshoptimizer.h",
      "dependencies/meshoptimizer/src/allocator.cpp",
      "dependencies/meshoptimizer/src/clusterizer.cpp",
      "dependencies/meshoptimizer/src/indexanalyzer.cpp",
      "dependencies/meshoptimizer/src/indexcodec.cpp",
      "dependencies/meshoptimizer/src/indexgenerator.cpp",
      "dependencies/meshoptimizer/src/meshletcodec.cpp",
      "dependencies/meshoptimizer/src/meshletutils.cpp",
      "dependencies/meshoptimizer/src/overdrawoptimizer.cpp",
      "dependencies/meshoptimizer/src/partition.cpp",
      "dependencies/meshoptimizer/src/quantization.cpp",
      "dependencies/meshoptimizer/src/rasterizer.cpp",
      "dependencies/meshoptimizer/src/simplifier.cpp",
      "dependencies/meshoptimizer/src/spatialorder.cpp",
      "dependencies/meshoptimizer/src/stripifier.cpp",
      "dependencies/meshoptimizer/src/vcacheoptimizer.cpp",
      "dependencies/meshoptimizer/src/vertexcodec.cpp",
      "dependencies/meshoptimizer/src/vertexfilter.cpp",
      "dependencies/meshoptimizer/src/vfetchoptimizer.cpp"
   }

   includedirs {
      "dependencies/meshoptimizer/src"
   }

   defines { "_CRT_SECURE_NO_WARNINGS", "MESHOPTIMIZER_API=__declspec(dllexport)" }

   postbuildcommands {
      "{MKDIR} ../bin/Editor/%{cfg.buildcfg}/x64",
      "{MKDIR} ../bin/Runtime/%{cfg.buildcfg}/x64",
      "{COPY} ../bin/meshoptimizer/%{cfg.buildcfg}/x64/meshoptimizer.dll ../bin/Editor/%{cfg.buildcfg}/x64/",
      "{COPY} ../bin/meshoptimizer/%{cfg.buildcfg}/x64/meshoptimizer.dll ../bin/Runtime/%{cfg.buildcfg}/x64/"
   }

   vfStandardConfigs()


-- Project: Recast Navigation (Navmesh generation + pathfinding)
project "recast"
   kind "StaticLib"
   language "C++"
   cppdialect "C++17"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/recastnavigation/Recast/Include/**.h",
      "dependencies/recastnavigation/Recast/Source/**.cpp",
      "dependencies/recastnavigation/Detour/Include/**.h",
      "dependencies/recastnavigation/Detour/Source/**.cpp",
      "dependencies/recastnavigation/DetourCrowd/Include/**.h",
      "dependencies/recastnavigation/DetourCrowd/Source/**.cpp"
   }

   includedirs {
      "dependencies/recastnavigation/Recast/Include",
      "dependencies/recastnavigation/Detour/Include",
      "dependencies/recastnavigation/DetourCrowd/Include"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()


-- Project: enkiTS (Task Scheduler for game engines)
project "enkiTS"
   kind "StaticLib"
   language "C++"
   cppdialect "C++17"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/enkiTS/src/TaskScheduler.h",
      "dependencies/enkiTS/src/TaskScheduler.cpp",
      "dependencies/enkiTS/src/LockLessMultiReadPipe.h",
      "dependencies/enkiTS/src/TaskScheduler_c.h",
      "dependencies/enkiTS/src/TaskScheduler_c.cpp"
   }

   includedirs {
      "dependencies/enkiTS/src"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()


-- Project: ISPCTextureCompressor (BC7/BC6H GPU texture compression)
project "ispc_texcomp"
   kind "StaticLib"
   language "C++"
   cppdialect "C++17"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/ispc_texcomp/ispc_texcomp.h",
      "dependencies/ispc_texcomp/ispc_texcomp.cpp"
   }

   includedirs {
      "dependencies/ispc_texcomp"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   vfStandardConfigs()


-- ============================================================================
-- External plugins — each plugin folder under plugins/ carries its own
-- premake5.lua and is auto-discovered here. To add a plugin: create
-- plugins/<Name>/ with <Name>.cpp, <Name>.vfplugin and a premake5.lua,
-- then re-run `premake5 vs2022`. No edits to this file needed.
-- ============================================================================
include "plugins/plugin_sdk.lua"  -- defines vfPluginProject() used by each plugin's premake5.lua
include "tools/export_sdk.lua"   -- adds `premake5 export-sdk` (packages the out-of-tree plugin SDK)
include "tools/package_release.lua"  -- adds `premake5 package-release` (assembles dist/VertexForge ship folder)

-- In-tree plugins compile against sdk/ (not engine source) so they continuously
-- validate the SDK package. Refresh it on every solution generation — after
-- changing engine headers, re-run `premake5 vs2022` before building plugins.
if _ACTION and _ACTION:startswith("vs") then
   vfExportPluginSDK()
end

group "Plugins"
for _, pluginDir in ipairs(os.matchdirs("plugins/*")) do
   if os.isfile(pluginDir .. "/premake5.lua") then
      include(pluginDir)
   end
end
group ""


-- Project: assimp and softal need to build with cmake...