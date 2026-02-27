workspace "VertexForge"
   configurations { "Debug", "Release" }
   platforms { "x64" }
   location "VFEngine"  -- Specify where to place generated files
   startproject "Editor"  -- Set the default startup project

   -- Target latest Windows SDK and VS2026 toolset (v145) to avoid retargeting dialog
   filter "system:windows"
      systemversion "latest"
      toolset "v145"
   filter {}

   -- Enable UTF-8 support for all C++ projects (required by spdlog/fmt)
   filter "language:C++"
      buildoptions { "/utf-8", "/MP" }
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

   files { "VFEngine/editor/**.hpp", "VFEngine/editor/**.cpp","resources/editor/**.vfImage" }

   includedirs {
	  "dependencies/imgui",
	  "dependencies/ImGuizmo",
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
	  "VFEngine/plugin"                   -- Plugin system
   }

   links {
      "Core",                           -- Link Core project
	  "Import",
	  "Services",                       -- Link Services project
	  "Plugin",                         -- Plugin system
	  "imgui"                           -- For imgui-node-editor in ShaderGraphEditor
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   -- Windows-specific libraries for splash screen
   filter "system:windows"
      links { "gdiplus" }
   filter {}

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"
      -- Copy OpenAL DLL to Editor output directory
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Debug/OpenAL32.dll ../../bin/Editor/Debug/x64/"
      }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      -- Copy OpenAL DLL to Editor output directory
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Release/OpenAL32.dll ../../bin/Editor/Release/x64/"
      }

-- Project 2: Core
project "Core"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/core"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/core/**.hpp", "VFEngine/core/**.cpp" }

   includedirs {
      "VFEngine/graphics/controllers",   -- Graphics headers
      "VFEngine/window/controllers",     -- Window headers
	  "dependencies/spdlog/include",
	  "dependencies/entt/single_include",
      "VFEngine/utilities",              -- Utilities headers
	  "VFEngine/services",               -- Services layer interfaces
	  "dependencies/imgui",
	  "dependencies/ImGuizmo",
	  "dependencies/glm",
	  "dependencies/glfw/include",
	  "dependencies/imgui/backends",
	  "dependencies/openal-soft/include", -- OpenAL headers
	  "dependencies/mtype/mType",         -- mType scripting language
	  vulkanLibPath.."/Include",
	  "dependencies/JoltPhysics",          -- Jolt Physics headers
	  "dependencies/recastnavigation/Recast/Include",   -- Recast navmesh generation
	  "dependencies/recastnavigation/Detour/Include",   -- Detour pathfinding
	  "dependencies/recastnavigation/DetourCrowd/Include" -- DetourCrowd agent steering
   }

   links { "Graphics", "mType", "jolt", "recast" }  -- Link against Graphics, mType, jolt, recast (Services is a higher layer, no link needed)
   defines { "_CRT_SECURE_NO_WARNINGS", "JPH_OBJECT_STREAM" }

   filter "configurations:Debug"
      defines { "DEBUG", "JPH_ENABLE_ASSERTS" }
      symbols "On"
      libdirs { "dependencies/openal-soft/build/Debug" }
      links { "OpenAL32.lib" }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      libdirs { "dependencies/openal-soft/build/Release" }
      links { "OpenAL32.lib" }
	  
	  
project "Import"
   kind "StaticLib"
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
	  "dependencies/freetype/include"    -- FreeType headers
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   links { "Utilities", "meshoptimizer" }

   -- Debug configuration
   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"
      libdirs { "dependencies/assimp/build/lib/Debug", "dependencies/freetype/build/Debug" }
      links { "assimp-vc145-mtd.lib", "freetyped.lib" }

    -- Copy the DLL to the Editor's output directory after the build
   postbuildcommands {
      "{COPY} ../../dependencies/assimp/build/bin/Debug/assimp-vc145-mtd.dll ../../bin/Editor/Debug/x64/"
   }

   -- Release configuration
   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      libdirs { "dependencies/assimp/build/lib/Release", "dependencies/freetype/build/Release" }
      links { "assimp-vc145-mt.lib", "freetype.lib" }

      -- Copy the DLL to the output directory after the build
      postbuildcommands {
         "{COPY} ../../dependencies/assimp/build/bin/Release/assimp-vc145-mt.dll ../../bin/Editor/Release/x64/"
      }


-- Project 3: Graphics
project "Graphics"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/graphics"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/graphics/**.hpp", "VFEngine/graphics/**.cpp" ,"resources/shaders/**.glsl"}

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
      vulkanLibPath.."/Include"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   libdirs {
      vulkanLibPath.."/Lib"
   }

   links {
      "Window",
	  "imgui"
   }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"
      links { "shaderc_shared.lib" }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      links { "shaderc_shared.lib" }

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
      "VFEngine/utilities",
      "VFEngine/services",              -- Services interfaces only
      "VFEngine/core/bootstrap",        -- For RuntimeBootstrap
      "VFEngine/plugin"                 -- Plugin system
      -- NOTE: NO VFEngine/core/controllers, NO VFEngine/graphics/controllers
   }

   links { "Services", "Core", "Plugin" }  -- Core linked for RuntimeBootstrap, not direct access

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"
      -- Copy OpenAL DLL to Runtime output directory
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Debug/OpenAL32.dll ../../bin/Runtime/Debug/x64/"
      }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      -- Copy OpenAL DLL to Runtime output directory
      postbuildcommands {
         "{COPY} ../../dependencies/openal-soft/build/Release/OpenAL32.dll ../../bin/Runtime/Release/x64/"
      }

-- Project 5: Utilities (Moved before Graphics)
project "Utilities"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/utilities"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/utilities/**.hpp", "VFEngine/utilities/**.cpp" }

   includedirs {
      "dependencies/spdlog/include",
      "dependencies/glm",
	  "dependencies/entt/single_include",
	  "dependencies/json/single_include",
	  "dependencies/meshoptimizer/src"  -- meshoptimizer for terrain meshlet generation
   }

   links { "spdLog", "meshoptimizer" }

   buildoptions { "/bigobj" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


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

   links { "Utilities", "Window" }  -- Window needed for InputServiceImpl

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


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
      "VFEngine/utilities",
      "VFEngine/services",
      "VFEngine/import/pipeline",          -- For PipelineStage base class
      "VFEngine/core/controllers"          -- For ImguiWindow base class
   }

   links { "Services", "Utilities" }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


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

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

-- Group for Libraries
group "libs"

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

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

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

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


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
      "dependencies/ImGuizmo/*.h",
      "dependencies/ImGuizmo/*.cpp",
      -- imgui-node-editor v0.9.3 flat structure
      "dependencies/imgui-node-editor/*.h",
      "dependencies/imgui-node-editor/*.cpp",
      "dependencies/imgui-node-editor/*.inl"
   }

   -- Exclude folders: misc and examples
   removefiles {
      "dependencies/imgui/misc/**",
      "dependencies/imgui/examples/**",
      "dependencies/ImGuizmo/examples/**",
      "dependencies/ImGuizmo/vcpkg-example/**",
      "dependencies/imgui-node-editor/examples/**",
      "dependencies/imgui-node-editor/external/**"
   }

   includedirs {
      "dependencies/imgui",                       -- Core ImGui headers
      "dependencies/imgui/backends",              -- Vulkan backend headers
      "dependencies/ImGuizmo",
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

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

-- Project: JoltPhysics (Moved under libs group)
project "jolt"
   kind "StaticLib"
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
      "JPH_OBJECT_STREAM"  -- Enable object serialization
   }

   filter "configurations:Debug"
      defines { "DEBUG", "JPH_ENABLE_ASSERTS" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


-- Project: mType (Scripting language interpreter)
project "mType"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files {
      "dependencies/mtype/mType/**.hpp",
      "dependencies/mtype/mType/**.cpp"
   }

   -- Exclude main entry point and tests (for standalone executable)
   removefiles {
      "dependencies/mtype/mType/run/**",
      "dependencies/mtype/mType/tests/**"
   }

   includedirs {
      "dependencies/mtype/mType",
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

   filter { "system:windows", "configurations:Release" }
      buildoptions { "/arch:AVX2" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


-- Project: meshoptimizer (Mesh simplification for LOD generation)
project "meshoptimizer"
   kind "StaticLib"
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

   defines { "_CRT_SECURE_NO_WARNINGS" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


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

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


-- Project: assimp and softal need to build with cmake...