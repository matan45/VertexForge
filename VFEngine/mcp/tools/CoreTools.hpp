#pragma once

#include "../dispatch/MainThreadQueue.hpp"
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

    // Registers all of the above. Called once on the main thread before start().
    void registerCoreTools(ToolRegistry& registry, MainThreadQueue& queue);
}
