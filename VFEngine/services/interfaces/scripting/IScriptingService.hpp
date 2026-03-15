#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/ScriptTypes.hpp"
#include "../../providers/scripting/IScriptingProvider.hpp"
#include <string>
#include <vector>

namespace services
{
    class IScriptingService
    {
    public:
        virtual ~IScriptingService() = default;

        virtual void registerEventHandlers() = 0;

        // === Script Building ===
        virtual ScriptBuildResult buildScripts() = 0;

        virtual void cleanScripts() = 0;

        virtual bool isCompiled() const = 0;

        // === Script Component (Multi-Script Support) ===
        virtual bool attachScript(EntityHandle entity, const ScriptData& data) = 0;
        virtual void detachScript(EntityHandle entity, const std::string& scriptPath) = 0;
        virtual std::vector<std::string> getScriptPaths(EntityHandle entity) const = 0;
        virtual void setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) = 0;
        virtual bool isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const = 0;

        virtual void updateScripts(float deltaTime) = 0;
        virtual void fixedUpdateScripts(float fixedDeltaTime) = 0;
        virtual void lateUpdateScripts(float deltaTime) = 0;
        virtual void stopAllScripts() = 0;
    };
}
