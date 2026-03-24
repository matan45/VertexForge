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
        nlohmann::json configData;
        std::string configFilePath;
        mutable std::mutex configMutex;
        bool loaded = false;

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
        void ensureLoaded();
        void flush();
        std::string getConfigPath() const;
    };
}
