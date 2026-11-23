#pragma once
#include "../../events/EventDispatcher.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <mutex>

namespace services
{
    class ConfigService
    {
    private:
        mutable nlohmann::json configData;
        mutable std::mutex configMutex;
        mutable bool loaded = false;
        std::filesystem::path stateFolder;
        bool handlersRegistered = false;

    public:
        // The config lives in <stateFolder>/config.json.
        // - An absolute stateFolder is used as it is, with or without a project (VK-1726: the Runtime passes
        //   its per-user %LOCALAPPDATA%\<Publisher>\<Game>, or the game's own folder for a portable build;
        //   see launch/GameStateLocation.hpp).
        // - A relative one is under the project folder, the .vfproj's folder; empty means the project folder
        //   itself. The editor passes ".vertexforge" (launch::editorStateDirName), so Play-mode config stays
        //   out of the content browser and of exported games (VK-1684). Without a project nothing is stored.
        explicit ConfigService(std::filesystem::path stateFolder);
        // Unregisters the handlers when this instance registered them (they capture `this`).
        ~ConfigService();

        ConfigService(const ConfigService&) = delete;
        ConfigService& operator=(const ConfigService&) = delete;

        void registerEventHandlers(::events::EventDispatcher& dispatcher);

        void setInt(const std::string& key, int64_t value);
        int64_t getInt(const std::string& key, int64_t defaultValue) const;

        void setFloat(const std::string& key, double value);
        double getFloat(const std::string& key, double defaultValue) const;

        void setString(const std::string& key, const std::string& value);
        std::string getString(const std::string& key, const std::string& defaultValue) const;

        void setBool(const std::string& key, bool value);
        bool getBool(const std::string& key, bool defaultValue) const;

    private:
        void ensureLoaded() const;
        void flush();
        std::string getConfigPath() const;
    };
}
