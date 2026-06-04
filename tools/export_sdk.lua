-- premake5 export-sdk
-- Packages the VertexForge Plugin SDK into sdk/ so plugins can be developed
-- OUTSIDE the engine source tree, against binaries + headers only (the same
-- model as native SDKs / Godot's godot-cpp: the engine itself is never linked —
-- plugins talk to it through the PluginContext vtable handed over at load time).
--
-- SDK layout:
--   sdk/include/VFEngine/...   engine headers (tree preserved so relative includes resolve)
--   sdk/deps/...               header-only third-party deps (glm, entt, json, spdlog, imgui)
--   sdk/lib/<Config>/imgui.lib only lib a plugin may need (editor ImGui windows)
--   sdk/template/              ready-to-copy out-of-tree plugin project
--   sdk/README.md              usage + ABI requirements

-- Callable directly (root premake runs this on every solution generation so the
-- in-tree plugins always compile against a fresh SDK) and via `premake5 export-sdk`.
function vfExportPluginSDK()
      local sdk = "sdk"

      local function copyTree(srcBase, pattern, dstBase)
         local files = os.matchfiles(path.join(srcBase, pattern))
         for _, f in ipairs(files) do
            local rel = path.getrelative(srcBase, f)
            local dst = path.join(dstBase, rel)
            os.mkdir(path.getdirectory(dst))
            os.copyfile(f, dst)
         end
         return #files
      end

      print("Exporting VertexForge Plugin SDK to " .. sdk .. "/ ...")

      -- Engine headers — keep the VFEngine/ tree intact so the SDK headers'
      -- relative includes ("../../services/data/...") resolve unchanged.
      local n = 0
      n = n + copyTree("VFEngine/plugin",           "**.hpp", sdk .. "/include/VFEngine/plugin")
      n = n + copyTree("VFEngine/services",         "**.hpp", sdk .. "/include/VFEngine/services")
      n = n + copyTree("VFEngine/utilities",        "**.hpp", sdk .. "/include/VFEngine/utilities")
      n = n + copyTree("VFEngine/core/controllers", "**.hpp", sdk .. "/include/VFEngine/core/controllers")
      print("  engine headers: " .. n)

      -- Header-only third-party dependencies
      n = 0
      n = n + copyTree("dependencies/glm/glm",            "**",        sdk .. "/deps/glm/glm")
      n = n + copyTree("dependencies/entt/single_include", "**.hpp",   sdk .. "/deps/entt")
      n = n + copyTree("dependencies/json/single_include", "**.hpp",   sdk .. "/deps/json")
      n = n + copyTree("dependencies/spdlog/include",      "**.h",     sdk .. "/deps/spdlog")
      n = n + copyTree("dependencies/imgui",               "*.h",      sdk .. "/deps/imgui")
      print("  third-party headers: " .. n)

      -- imgui.lib (only needed by plugins that register editor ImGui windows)
      for _, cfg in ipairs({"Debug", "Release"}) do
         local lib = "bin/imgui/" .. cfg .. "/x64/imgui.lib"
         if os.isfile(lib) then
            os.mkdir(sdk .. "/lib/" .. cfg)
            os.copyfile(lib, sdk .. "/lib/" .. cfg .. "/imgui.lib")
            print("  lib: " .. lib)
         else
            print("  WARNING: " .. lib .. " not built - run a " .. cfg .. " build first")
         end
      end

      -- Out-of-tree project template
      os.mkdir(sdk .. "/template")
      io.writefile(sdk .. "/template/premake5.lua", [[
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
   configurations { "Debug", "Release" }
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

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
]])

      io.writefile(sdk .. "/template/MyPlugin.vfplugin", [[
{
    "apiVersion": 7,
    "author": "you",
    "capabilities": ["graphics", "terrain", "input", "editor"],
    "dependencies": [],
    "description": "Out-of-tree plugin built against the VertexForge SDK",
    "enabled": true,
    "library": "MyPlugin.dll",
    "loadOrder": 200,
    "name": "MyPlugin",
    "version": "1.0.0"
}
]])

      io.writefile(sdk .. "/README.md", [[
# VertexForge Plugin SDK

Develop engine plugins outside the source tree — only this SDK folder and the
engine binaries (Editor.exe) are needed. Plugins never link the engine: all
access goes through the `plugin::PluginContext*` vtable handed over at load
time (same idea as Godot's GDExtension function table).

## Quick start
1. Copy `template/` somewhere, rename `MyPlugin` everywhere (premake5.lua,
   .vfplugin, your .cpp class).
2. Set env vars: `VERTEXFORGE_SDK` (this folder), `VERTEXFORGE_PATH` (engine
   root containing `plugins/`), `VULKAN_SDK`.
3. `premake5 vs2022`, then build. The postbuild step deploys the DLL +
   `.vfplugin` into the engine's `plugins/<Name>/` — start the editor and the
   PluginManager picks it up.

## ABI requirements (must match the engine build)
- MSVC toolset v145 (VS2026), x64, C++20, /MD runtime, `/utf-8`
- `apiVersion` in the `.vfplugin` must equal the engine's
  `VF_PLUGIN_API_VERSION` (`include/VFEngine/plugin/api/PluginVersion.hpp`)
- Plugins must NOT call Vulkan directly (no dispatcher in plugin DLLs) — use
  the custom render pipeline API (`createCustomPipeline`/`uploadCustomMesh`/
  `drawCustomMesh`) instead.
- For editor ImGui windows: `links { "imgui" }` (in `lib/`), call
  `ImGui::SetCurrentContext(ctx->getImGuiContext())` in `onInitialize`, then
  `ctx->registerEditorWindow(window, "Title")`.

Regenerate this SDK after engine API changes: `premake5 export-sdk` in the
engine repo (re-run a Debug/Release build first so `lib/` is current).
]])

      print("Done. SDK at " .. path.getabsolute(sdk))
end

newaction {
   trigger = "export-sdk",
   description = "Package the VertexForge plugin SDK (headers + libs + template) into sdk/",
   execute = vfExportPluginSDK
}
