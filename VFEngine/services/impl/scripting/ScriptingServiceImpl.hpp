#pragma once
#include "../../interfaces/scripting/IScriptingService.hpp"
#include "../../providers/scripting/IScriptingProvider.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "components/Components.hpp"
#include "scripting/ScriptTickGovernor.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

namespace services
{
    class ScriptingServiceImpl : public IScriptingService
    {
    private:
        IScriptingProvider* scriptingProvider;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        struct ScriptUpdateEntry
        {
            entt::entity entity;
            size_t scriptIndex;
            int priority;
        };
        std::vector<ScriptUpdateEntry> cachedUpdateList;
        // Rebuild + sort cachedUpdateList only when the script set changes
        // (attach/detach, entity create/destroy, priority edits) instead of
        // every frame — set by mutators and the ScriptComponent registry hooks
        bool scriptListDirty = true;

        // VK-1536 — distance/significance rate scaling. Disabled by default; the per-script
        // updateInterval works on its own without this.
        scripting::ScriptLODConfig scriptLOD;

        // VK-1536 — reused scratch so the per-script crash breadcrumb costs no heap traffic.
        // std::string keeps its capacity across clear()/assign(), so these allocate once and then
        // never again, instead of building fresh temporaries for every script every frame.
        std::string crashContextScratch;
        std::string entityNameScratch;

        void rebuildScriptUpdateList(entt::registry& registry);
        void onScriptComponentChanged(entt::registry& registry, entt::entity entity);

        // VK-1536 — camera position for distance scaling, resolved once per updateScripts call.
        // Returns false when there is no usable camera, in which case nothing is throttled by
        // distance (the authored interval still applies).
        bool queryCameraPosition(glm::vec3& outPos) const;

        std::string getManifestPath() const;

    public:
        explicit ScriptingServiceImpl(IScriptingProvider* scriptingProvider,
                                      std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~ScriptingServiceImpl() override;

        void registerEventHandlers() override;

        // === Script Building ===
        ScriptBuildResult buildScripts() override;
        void cleanScripts() override;
        bool isCompiled() const override;

        // === Script Component (Multi-Script Support) ===
        bool attachScript(EntityHandle entity, const ScriptData& data) override;
        void detachScript(EntityHandle entity, const std::string& scriptPath) override;
        std::vector<std::string> getScriptPaths(EntityHandle entity) const override;
        void setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) override;
        bool isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const override;

        void updateScripts(float deltaTime) override;
        void fixedUpdateScripts(float fixedDeltaTime) override;
        void lateUpdateScripts(float deltaTime) override;
        void stopAllScripts() override;
    };
}
