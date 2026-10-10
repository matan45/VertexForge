#pragma once

#include "../dispatch/MainThreadQueue.hpp"
#include "../protocol/PromptRegistry.hpp"
#include "../protocol/ResourceRegistry.hpp"
#include "../protocol/ToolRegistry.hpp"

#include <chrono>
#include <functional>

namespace mcp::tools
{
    // Shared by tool groups. Main-affinity tools run on the editor main thread
    // already; Worker-affinity tools use runOnMain() for any EventDispatcher call.
    struct ToolContext
    {
        MainThreadQueue& queue;

        nlohmann::json runOnMain(std::function<nlohmann::json()> task,
                                 std::chrono::milliseconds timeout = std::chrono::milliseconds(10000)) const
        {
            return queue.invoke(std::move(task), timeout);
        }
    };

    // Every engine-facing tool group. Each lives in its own TU under tools/.
    void registerEditorTools(ToolRegistry& registry, const ToolContext& context);     // editor_get_info, project_*
    void registerSceneTools(ToolRegistry& registry, const ToolContext& context);      // scene_*
    void registerEntityTools(ToolRegistry& registry, const ToolContext& context);     // entity_*
    void registerComponentTools(ToolRegistry& registry, const ToolContext& context);  // component_*
    void registerMaterialTools(ToolRegistry& registry, const ToolContext& context);   // material_*
    void registerScriptTools(ToolRegistry& registry, const ToolContext& context);     // script_*, scripts_build
    void registerAssetTools(ToolRegistry& registry, const ToolContext& context);      // assets_*
    void registerPlayModeTools(ToolRegistry& registry, const ToolContext& context);   // play_*
    void registerLogTools(ToolRegistry& registry, const ToolContext& context);        // logs_read
    void registerUndoTools(ToolRegistry& registry, const ToolContext& context);       // undo, redo
    void registerViewTools(ToolRegistry& registry, const ToolContext& context);       // viewport_screenshot (VK-1651)
    void registerPluginComponentTools(ToolRegistry& registry, const ToolContext& context); // component_*_generic, component_list_types (VK-1651)
    void registerExportTools(ToolRegistry& registry, const ToolContext& context);     // game_export, game_export_status (VK-1651)
    void registerTerrainTools(ToolRegistry& registry, const ToolContext& context);    // terrain_* (VK-1653)

    // Registers all of the above. Called once on the main thread before start().
    void registerCoreTools(ToolRegistry& registry, MainThreadQueue& queue);

    // VK-1652 (ResourceDefs.cpp): vf://scene/hierarchy, vf://logs, vf://scripts/{+path},
    // vf://docs/components, vf://docs/mtype-api, vf://docs/mtype-api/{+module}.
    void registerCoreResources(ResourceRegistry& registry, MainThreadQueue& queue);

    // VK-1652 (PromptDefs.cpp): create_platformer_template, create_top_down_template.
    void registerCorePrompts(PromptRegistry& registry);
}
