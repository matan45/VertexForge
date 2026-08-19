#pragma once
#include "../../events/EventDispatcher.hpp"
#include <nlohmann/json.hpp>
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
        // The config file lives next to the open project, so the cache has to be keyed by it:
        // a read taken before any project is open (or after switching projects) would otherwise
        // latch `loaded` against the wrong file and the next write would overwrite the real
        // config with the stale in-memory object.
        mutable std::string loadedPath;

    public:
        ConfigService();
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
