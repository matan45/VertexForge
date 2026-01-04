#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include "../providers/IScriptingProvider.hpp"
#include <string>
#include <vector>

namespace services {

    class IScriptingService {
    public:
        virtual ~IScriptingService() = default;

        // === Script Building ===
        // Build all scripts from manifest
        virtual ScriptBuildResult buildScripts() = 0;

        // Clean compiled scripts
        virtual void cleanScripts() = 0;

        // Check if scripts are compiled and ready
        virtual bool isCompiled() const = 0;

        // Set progress callback for build operations
        virtual void setBuildProgressCallback(ScriptBuildProgressCallback callback) = 0;

        // === Script Component (Multi-Script Support) ===
        virtual bool attachScript(EntityHandle entity, const ScriptData& data) = 0;
        virtual void detachScript(EntityHandle entity, const std::string& scriptPath) = 0;
        virtual void detachAllScripts(EntityHandle entity) = 0;
        virtual bool hasScripts(EntityHandle entity) const = 0;
        virtual bool hasScript(EntityHandle entity, const std::string& scriptPath) const = 0;
        virtual std::vector<std::string> getScriptPaths(EntityHandle entity) const = 0;
        virtual void setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) = 0;
        virtual bool isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const = 0;

        // === System Update ===
        virtual void updateScripts(float deltaTime) = 0;

        // === Script Lifecycle Events ===
        virtual void triggerStart(EntityHandle entity) = 0;
        virtual void triggerDestroy(EntityHandle entity) = 0;
    };

}
