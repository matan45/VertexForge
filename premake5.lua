workspace "VertexForge"
   configurations { "Debug", "Release" }
   platforms { "x64" }
   location "VFEngine"  -- Specify where to place generated files
   startproject "Editor"  -- Set the default startup project

   -- Enable UTF-8 support for all C++ projects (required by spdlog/fmt)
   filter "language:C++"
      buildoptions { "/utf-8" }
   filter {}

-- Check if the Vulkan SDK environment variable is set
local vulkanLibPath = os.getenv("VULKAN_SDK")
if not vulkanLibPath then
   error("VULKAN_SDK environment variable is not set.")
end

-- Group for Engine Projects
group "Engine"

-- Project 1: Editor
project "Editor"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/editor"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/editor/**.hpp", "VFEngine/editor/**.cpp" }
   
   includedirs {
	  "dependencies/imgui",
	  "dependencies/ImGuizmo",
	  "dependencies/ImGuiColorTextEdit",
	  "dependencies/imgui-node-editor",
	  "dependencies/spdlog/include",
	  "dependencies/glm",
	  "dependencies/entt/single_include",
	  "VFEngine/utilities",
	  "VFEngine/core/controllers",
	  "dependencies/IconFontCppHeaders",
	  "VFEngine/import/controllers",
	  "VFEngine/services"               -- Services layer interfaces
   }

   links {
      "Core",                           -- Link Core project
	  "Import",
	  "Services"                        -- Link Services project
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

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
	  "dependencies/glm",
	  "dependencies/glfw/include",
	  "dependencies/imgui/backends",
	  vulkanLibPath.."/Include"
   }

   links { "Graphics" }  -- Link against Graphics (Services is a higher layer, no link needed)
   defines { "_CRT_SECURE_NO_WARNINGS" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
	  
	  
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
      "dependencies/assimp/include",     -- Assimp headers
	  "dependencies/glm",
	  
   }
   
   defines { "_CRT_SECURE_NO_WARNINGS" }

   links { "Utilities" }

   -- Debug configuration
   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"
      libdirs { "dependencies/assimp/lib/Debug" }
      links { "assimp-vc143-mtd.lib" }  -- Assimp Debug library

    -- Copy the DLL to the Editor's output directory after the build
   postbuildcommands {
      "{COPY} ../../dependencies/assimp/bin/Debug/assimp-vc143-mtd.dll ../../bin/Editor/Debug/x64/"
   }

   -- Release configuration
   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      libdirs { "dependencies/assimp/lib/Release" }
      links { "assimp-vc145-mt.lib" }  -- Assimp Release library

      -- Copy the DLL to the output directory after the build
      postbuildcommands {
         "{COPY} ../../dependencies/assimp/bin/Release/assimp-vc145-mt.dll ../../bin/Editor/Release/x64/"
      }


-- Project 3: Graphics
project "Graphics"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/graphics"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/graphics/**.hpp", "VFEngine/graphics/**.cpp" }

   includedirs {
      "dependencies/glfw/include",
      "dependencies/spdlog/include",
	  "dependencies/imgui",
	  "dependencies/imgui/backends",
      "dependencies/glm",
	  "dependencies/entt/single_include",
      "dependencies/stb",
      "VFEngine/utilities",           -- Utilities headers
      "VFEngine/window/controllers",           -- Utilities headers
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

-- Project 4: Runtime (Moved after Core and Graphics)
project "Runtime"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   location "VFEngine/runtime"
   targetdir "bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

   files { "VFEngine/runtime/**.hpp", "VFEngine/runtime/**.cpp" }

   includedirs {
      "VFEngine/core/controllers",
      "VFEngine/graphics/controllers"
   }

   links { "Core"}  -- Link against Core and Graphics

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

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
	  "dependencies/json/single_include"
   }

   links { "spdLog" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


-- Project: Services (Event System, Service Interfaces, Service Implementations)
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
      "dependencies/glfw/include",
      "dependencies/imgui",
      "dependencies/json/single_include",
      "VFEngine/utilities",
      "VFEngine/core/controllers",
      "VFEngine/Window",
      vulkanLibPath.."/Include"
   }

   links { "Utilities", "Window" }  -- Import is Editor-level, not Services dependency

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
      "dependencies/imgui-node-editor/*.h",
      "dependencies/ImGuiColorTextEdit/*.h",
      "dependencies/ImGuiColorTextEdit/*.cpp",
      "dependencies/imgui-node-editor/*.cpp"
   }

   -- Exclude folders: misc and examples
   removefiles {
      "dependencies/imgui/misc/**",
      "dependencies/imgui/examples/**",
      "dependencies/ImGuizmo/examples/**",
      "dependencies/ImGuizmo/vcpkg-example/**",
      "dependencies/imgui-node-editor/external/**",
      "dependencies/imgui-node-editor/misc/**",
      "dependencies/imgui-node-editor/examples/**",
   }

   includedirs {
      "dependencies/imgui",                       -- Core ImGui headers
      "dependencies/imgui/backends",              -- Vulkan backend headers
      "dependencies/ImGuizmo",              
      "dependencies/imgui-node-editor", 
	  "dependencies/glfw/include",	  
	  "dependencies/ImGuiColorTextEdit/*.h",	  
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
   

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"



-- Project: assimp and softal need to build with cmake...