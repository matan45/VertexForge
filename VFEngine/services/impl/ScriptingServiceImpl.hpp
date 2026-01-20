#pragma once
#include "../interfaces/IScriptingService.hpp"
#include "../providers/IScriptingProvider.hpp"
#include "../../utilities/scene/SceneGraphSystem.hpp"

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
        void detachAllScripts(EntityHandle entity) override;
        bool hasScripts(EntityHandle entity) const override;
        bool hasScript(EntityHandle entity, const std::string& scriptPath) const override;
        std::vector<std::string> getScriptPaths(EntityHandle entity) const override;
        void setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) override;
        bool isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const override;

        // === System Update ===
        void updateScripts(float deltaTime) override;
        void stopAllScripts() override;

        // === Script Playback Control ===
        void playScript(EntityHandle entity, const std::string& scriptPath) override;
        void pauseScript(EntityHandle entity, const std::string& scriptPath) override;
        void stopScript(EntityHandle entity, const std::string& scriptPath) override;
        void resetScript(EntityHandle entity, const std::string& scriptPath) override;

        ScriptPlaybackState getScriptPlaybackState(EntityHandle entity, const std::string& scriptPath) const override;
        void setScriptPlaybackParams(EntityHandle entity, const std::string& scriptPath, const ScriptPlaybackParams& params) override;
        ScriptPlaybackParams getScriptPlaybackParams(EntityHandle entity, const std::string& scriptPath) const override;
        bool isScriptPlaying(EntityHandle entity, const std::string& scriptPath) const override;

        void playAllScriptsOnEntity(EntityHandle entity) override;
        void pauseAllScriptsOnEntity(EntityHandle entity) override;
        void stopAllScriptsOnEntity(EntityHandle entity) override;
    };
}
