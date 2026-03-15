#pragma once
#include "../../interfaces/scripting/IScriptingService.hpp"
#include "../../providers/scripting/IScriptingProvider.hpp"
#include "scene/SceneGraphSystem.hpp"

namespace services
{
    class ScriptingServiceImpl : public IScriptingService
    {
    private:
        IScriptingProvider* scriptingProvider;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

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
