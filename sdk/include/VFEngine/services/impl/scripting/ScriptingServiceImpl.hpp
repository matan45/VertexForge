#pragma once
#include "../../interfaces/scripting/IScriptingService.hpp"
#include "../../providers/scripting/IScriptingProvider.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "components/Components.hpp"
#include "scripting/ScriptTickGovernor.hpp"
#include "../../events/EventTypes.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <atomic>
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

        // VK-1536 — reused scratch so the per-script crash breadcrumb costs no heap traffic.
        // std::string keeps its capacity across clear()/assign(), so these allocate once and then
        // never again, instead of building fresh temporaries for every script every frame.
        std::string crashContextScratch;
        std::string entityNameScratch;

        // A full-scene clear (Scene::load, New Scene, Play/Stop snapshot restore) destroys
        // entities WITHOUT going through detachScript, so their script instances would live
        // on in the provider and never receive onDestroy. SceneClearedNotification only raises
        // this flag; the sweep itself runs at the top of updateScripts so onDestroy is always
        // invoked on the thread that owns the interpreter (the Scripts task), never on whatever
        // thread happened to publish the notification.
        std::atomic<bool> orphanSweepPending{false};
        ::events::SubscriptionToken sceneClearedToken;

        void rebuildScriptUpdateList(entt::registry& registry);
        void onScriptComponentChanged(entt::registry& registry, entt::entity entity);
        void sweepOrphanedScripts();

        std::string getManifestPath() const;

    public:
        explicit ScriptingServiceImpl(IScriptingProvider* scriptingProvider,
                                      std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~ScriptingServiceImpl() override;

        void registerEventHandlers() override;

        // === Script Building ===
        ScriptBuildResult buildScripts() override;
        bool loadCompiledScripts() override;
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
