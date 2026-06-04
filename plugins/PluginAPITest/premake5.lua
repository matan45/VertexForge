-- PluginAPITest plugin — self-contained premake project.
-- Auto-discovered by the root premake5.lua (any plugins/<Name>/premake5.lua is included).
local vulkanSDK = os.getenv("VULKAN_SDK")
if not vulkanSDK then
   error("VULKAN_SDK environment variable is not set.")
end

project "PluginAPITest"
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
      "../../VFEngine/services"
   }

   defines { "_CRT_SECURE_NO_WARNINGS" }

   postbuildcommands {
      "{COPY} ../../bin/PluginAPITest/%{cfg.buildcfg}/%{cfg.platform}/PluginAPITest.dll ../../plugins/PluginAPITest/"
   }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
