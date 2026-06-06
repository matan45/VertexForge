-- Out-of-tree VertexForge plugin. Requires:
--   VERTEXFORGE_SDK  -> path to the exported sdk/ folder
--   VERTEXFORGE_PATH -> engine root (where Editor.exe's plugins/ folder lives), for deploy
--   VULKAN_SDK       -> Vulkan SDK (headers pulled transitively by some engine headers)
-- Build: premake5 vs2022 && msbuild <Name>.sln /p:Configuration=Debug /p:Platform=x64

local PLUGIN_NAME = "MyPlugin"   -- <<< rename (must match .vfplugin "name" and "library")

local sdkDir = os.getenv("VERTEXFORGE_SDK")    or error("VERTEXFORGE_SDK not set")
local engineDir = os.getenv("VERTEXFORGE_PATH") or error("VERTEXFORGE_PATH not set")
local vulkanSDK = os.getenv("VULKAN_SDK")       or error("VULKAN_SDK not set")

workspace (PLUGIN_NAME)
   configurations { "Debug", "Development", "Release" }
   platforms { "x64" }
   location "."

   -- ABI must match the engine: MSVC v145, C++20, /MD runtime (premake default),
   -- /utf-8 (spdlog/fmt requirement), same engine-wide defines.
   filter "system:windows"
      systemversion "latest"
      toolset "v145"
   filter "language:C++"
      buildoptions { "/utf-8", "/MP" }
      defines { "VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1", "GLM_FORCE_DEPTH_ZERO_TO_ONE" }
   filter {}

project (PLUGIN_NAME)
   kind "SharedLib"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{cfg.buildcfg}"

   files { "**.hpp", "**.cpp" }

   includedirs {
      sdkDir .. "/include/VFEngine/plugin",
      sdkDir .. "/include/VFEngine/utilities",
      sdkDir .. "/include/VFEngine/services",
      sdkDir .. "/include/VFEngine/core/controllers",
      sdkDir .. "/deps/glm",
      sdkDir .. "/deps/entt",
      sdkDir .. "/deps/json",
      sdkDir .. "/deps/spdlog",
      sdkDir .. "/deps/imgui",
      vulkanSDK .. "/Include"
   }

   libdirs { sdkDir .. "/lib/%{cfg.buildcfg}" }
   links { "imgui" }   -- remove if the plugin has no editor ImGui window

   defines { "_CRT_SECURE_NO_WARNINGS" }

   -- Deploy: DLL + descriptor into the engine's plugins/ folder
   postbuildcommands {
      "{MKDIR} \"" .. engineDir .. "/plugins/" .. PLUGIN_NAME .. "\"",
      "{COPY} \"%{cfg.buildtarget.abspath}\" \"" .. engineDir .. "/plugins/" .. PLUGIN_NAME .. "/\"",
      "{COPY} \"%{prj.location}/" .. PLUGIN_NAME .. ".vfplugin\" \"" .. engineDir .. "/plugins/" .. PLUGIN_NAME .. "/\""
   }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Development"
      defines { "NDEBUG", "VF_DEVELOPMENT" }
      optimize "On"
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
