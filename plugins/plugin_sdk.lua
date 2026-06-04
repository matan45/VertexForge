-- Shared project definition for external plugins (the "plugin SDK" build setup).
-- Loaded once by the root premake5.lua before plugin discovery; each plugin's
-- premake5.lua then just calls:
--
--    vfPluginProject("MyPlugin")
--       links { "imgui" }   -- optional plugin-specific extras (editor UI, etc.)
--
-- Paths are relative to the plugin folder (plugins/<Name>/), which is the
-- working directory while a plugin's premake5.lua runs.

local vulkanSDK = os.getenv("VULKAN_SDK")
if not vulkanSDK then
   error("VULKAN_SDK environment variable is not set.")
end

function vfPluginProject(name)
   project(name)
      kind "SharedLib"
      language "C++"
      cppdialect "C++20"
      location "."
      targetdir "../../bin/%{prj.name}/%{cfg.buildcfg}/%{cfg.platform}"

      files { "**.hpp", "**.cpp" }

      includedirs {
         "../../dependencies/spdlog/include",
         "../../dependencies/glm",
         "../../dependencies/entt/single_include",
         "../../dependencies/imgui",
         "../../dependencies/json/single_include",
         vulkanSDK.."/Include",
         "../../VFEngine/plugin",
         "../../VFEngine/utilities",
         "../../VFEngine/services",
         "../../VFEngine/core/controllers"
      }

      defines { "_CRT_SECURE_NO_WARNINGS" }

      postbuildcommands {
         "{COPY} ../../bin/" .. name .. "/%{cfg.buildcfg}/%{cfg.platform}/" .. name .. ".dll ../../plugins/" .. name .. "/"
      }

      filter "configurations:Debug"
         defines { "DEBUG" }
         symbols "On"

      filter "configurations:Release"
         defines { "NDEBUG" }
         optimize "On"

      filter {}
end
