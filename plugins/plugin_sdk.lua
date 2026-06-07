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

      -- Plugins compile against the exported SDK (sdk/), exactly like an
      -- out-of-tree plugin would — this keeps the SDK package honest: a header
      -- missing from the export breaks the in-tree build immediately.
      -- The root premake5.lua re-exports sdk/ on every solution generation.
      includedirs {
         "../../sdk/include/VFEngine/plugin",
         "../../sdk/include/VFEngine/utilities",
         "../../sdk/include/VFEngine/services",
         "../../sdk/include/VFEngine/core/controllers",
         "../../sdk/deps/glm",
         "../../sdk/deps/entt",
         "../../sdk/deps/json",
         "../../sdk/deps/spdlog",
         "../../sdk/deps/imgui",
         "../../sdk/deps/mType",
         vulkanSDK.."/Include"
      }

      defines { "_CRT_SECURE_NO_WARNINGS" }

      postbuildcommands {
         "{COPY} ../../bin/" .. name .. "/%{cfg.buildcfg}/%{cfg.platform}/" .. name .. ".dll ../../plugins/" .. name .. "/"
      }

      -- Same per-config settings as the engine (vfStandardConfigs is defined in
      -- the root premake5.lua, which includes this file). Plugins must be built
      -- in the same config as the Editor (/MDd vs /MD ABI).
      vfStandardConfigs()
end
