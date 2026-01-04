#pragma once
#include "../interfaces/IScriptingService.hpp"
#include "../providers/IScriptingProvider.hpp"
#include "../../utilities/scene/SceneGraphSystem.hpp"

namespace services {

    class ScriptingServiceImpl : public IScriptingService {
    public:
        explicit ScriptingServiceImpl(IScriptingProvider* scriptingProvider,
                                       std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~ScriptingServiceImpl() override;

        void registerEventHandlers();

        // === Script Component (Multi-Script Support) ===
        bool attachScript(EntityHandle entity, const ScriptData& data) override;
        void detachScript(EntityHandle entity, const std::string& scriptPath) override;
        void detachAllScripts(EntityHandle entity) override;
        bool hasScripts(EntityHandle entity) const override;
        bool hasScript(EntityHandle entity, const std::string& scriptPath) const override;
        std::vector<std::string> getScriptPaths(EntityHandle entity) const override;
        void setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) override;
        bool isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const override;

        // === System Update ===
        void updateScripts(float deltaTime) override;

        // === Script Lifecycle Events ===
        void triggerStart(EntityHandle entity) override;
        void triggerDestroy(EntityHandle entity) override;

    private:
        IScriptingProvider* scriptingProvider;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
    };

}
