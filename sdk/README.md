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
- For mType script natives (`ctx->registerScriptFunction`, scripting
  capability): build an `environment::registry::NativeDelegate`
  (`deps/mType/environment/registry/NativeDelegate.hpp`), wrap it in
  `std::any`. Primitive `value::Value` use (int/float/bool) is header-inline —
  no extra lib. Do NOT return strings/objects/arrays from plugin natives
  (those need mType's engine-side global pools). Functions auto-unregister on
  plugin unload.

Regenerate this SDK after engine API changes: `premake5 export-sdk` in the
engine repo (re-run a Debug/Release build first so `lib/` is current).
