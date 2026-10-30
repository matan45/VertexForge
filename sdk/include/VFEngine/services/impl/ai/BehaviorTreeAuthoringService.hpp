#pragma once

#include "../../data/BehaviorTreeAuthoringData.hpp"
#include "../../events/ai/BehaviorTreeAuthoringEvents.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace services
{
    // VK-1660 - answers events::ai::LoadBehaviorTreeAssetQuery and SaveBehaviorTreeAssetCommand: the
    // strict-validated, hash-guarded write path the MCP bt_* tools use for .vfBehaviorTree assets (the BT
    // editor keeps its own permissive save). Reads the file on every call; holds no state besides its
    // handler registration. Editor only.
    class BehaviorTreeAuthoringService
    {
    public:
        // The script source root ScriptTask paths resolve against (ScriptingAdapter's manifest folder);
        // an empty path when no project is loaded.
        using ScriptsRootProvider = std::function<std::filesystem::path()>;

        // Without `rootProvider`, the root is the current project's <workingDirectory>/scripts.
        explicit BehaviorTreeAuthoringService(ScriptsRootProvider rootProvider = {});
        ~BehaviorTreeAuthoringService();

        BehaviorTreeAuthoringService(const BehaviorTreeAuthoringService&) = delete;
        BehaviorTreeAuthoringService& operator=(const BehaviorTreeAuthoringService&) = delete;

        void registerEventHandlers();
        void unregisterEventHandlers();

        static BehaviorTreeAssetLoadResult loadAsset(const std::string& path);
        BehaviorTreeAssetSaveResult saveAsset(const ::events::ai::SaveBehaviorTreeAssetCommand& cmd) const;

        // The @Script class of the ScriptTask at `scriptPath`, resolved like ScriptingAdapter::loadScript
        // ("<scripts root>/<scriptPath>", else the path as given); nullopt when the file is missing,
        // unreadable or not a ScriptTask (behaviortree::scanScriptTask).
        std::optional<std::string> scriptTaskClass(std::string_view scriptPath) const;

    private:
        ScriptsRootProvider scriptsRoot;
        bool handlersRegistered = false;
    };
}
