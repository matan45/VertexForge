#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include "../providers/IScriptingProvider.hpp"
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
        virtual void detachAllScripts(EntityHandle entity) = 0;
        virtual bool hasScripts(EntityHandle entity) const = 0;
        virtual bool hasScript(EntityHandle entity, const std::string& scriptPath) const = 0;
        virtual std::vector<std::string> getScriptPaths(EntityHandle entity) const = 0;
        virtual void setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) = 0;
        virtual bool isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const = 0;

        // === System Update ===
        virtual void updateScripts(float deltaTime) = 0;

        // Stop all scripts (called when exiting play mode)
        virtual void stopAllScripts() = 0;

        // === Script Playback Control ===
        virtual void playScript(EntityHandle entity, const std::string& scriptPath) = 0;
        virtual void pauseScript(EntityHandle entity, const std::string& scriptPath) = 0;
        virtual void stopScript(EntityHandle entity, const std::string& scriptPath) = 0;
        virtual void resetScript(EntityHandle entity, const std::string& scriptPath) = 0;

        virtual ScriptPlaybackState getScriptPlaybackState(EntityHandle entity, const std::string& scriptPath) const = 0;
        virtual void setScriptPlaybackParams(EntityHandle entity, const std::string& scriptPath, const ScriptPlaybackParams& params) = 0;
        virtual ScriptPlaybackParams getScriptPlaybackParams(EntityHandle entity, const std::string& scriptPath) const = 0;
        virtual bool isScriptPlaying(EntityHandle entity, const std::string& scriptPath) const = 0;

        // Batch operations for entity
        virtual void playAllScriptsOnEntity(EntityHandle entity) = 0;
        virtual void pauseAllScriptsOnEntity(EntityHandle entity) = 0;
        virtual void stopAllScriptsOnEntity(EntityHandle entity) = 0;
    };
}
