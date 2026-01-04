#pragma once
#include "../interfaces/IScriptingService.hpp"
#include "../providers/IScriptingProvider.hpp"
#include "../../utilities/scene/SceneGraphSystem.hpp"

namespace services {

    class ScriptingServiceImpl : public IScriptingService {
   
    private:
        IScriptingProvider* scriptingProvider;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        //TODO when we have the project file ge the path from there
        static constexpr const char* DEFAULT_MANIFEST_PATH = "C:/matan/VertexForge/assets/scripts/scripts.mtproj";
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

    
    };

}
