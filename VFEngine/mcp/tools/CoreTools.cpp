#include "CoreTools.hpp"

namespace mcp::tools
{
    void registerCoreTools(ToolRegistry& registry, MainThreadQueue& queue)
    {
        const ToolContext context{queue};
        registerEditorTools(registry, context);
        registerSceneTools(registry, context);
        registerEntityTools(registry, context);
        registerComponentTools(registry, context);
        registerMaterialTools(registry, context);
        registerScriptTools(registry, context);
        registerAssetTools(registry, context);
        registerPlayModeTools(registry, context);
        registerLogTools(registry, context);
        registerUndoTools(registry, context);
    }
}
