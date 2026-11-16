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

    public:
        // The config lives in <project folder>/<stateFolder>/config.json, the project folder being the
        // .vfproj's folder; an empty stateFolder means the project folder itself. The editor passes
        // ".vertexforge" (launch::editorStateDirName), so Play-mode config stays out of the content
        // browser and of exported games; the Runtime keeps it beside its game's .vfproj.
        explicit ConfigService(std::filesystem::path stateFolder);
        ~ConfigService() = default;

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
