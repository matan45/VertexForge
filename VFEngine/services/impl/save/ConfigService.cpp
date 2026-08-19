#include "ConfigService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/save/ConfigEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "print/Log.hpp"
#include "resource/VFSHelpers.hpp"
#include <fstream>
#include <filesystem>

namespace services
{
    using json = nlohmann::json;

    ConfigService::ConfigService()
    {
        configData = json::object();
    }

    void ConfigService::registerEventHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<::events::save::SetConfigIntCommand>(
            [this](const ::events::save::SetConfigIntCommand& cmd)
            {
                setInt(cmd.key, cmd.value);
                return true;
            });

        dispatcher.registerQueryHandler<::events::save::GetConfigIntQuery>(
            [this](const ::events::save::GetConfigIntQuery& q)
            {
                return getInt(q.key, q.defaultValue);
            });

        dispatcher.registerCommandHandler<::events::save::SetConfigFloatCommand>(
            [this](const ::events::save::SetConfigFloatCommand& cmd)
            {
                setFloat(cmd.key, cmd.value);
                return true;
            });

        dispatcher.registerQueryHandler<::events::save::GetConfigFloatQuery>(
            [this](const ::events::save::GetConfigFloatQuery& q)
            {
                return getFloat(q.key, q.defaultValue);
            });

        dispatcher.registerCommandHandler<::events::save::SetConfigStringCommand>(
            [this](const ::events::save::SetConfigStringCommand& cmd)
            {
                setString(cmd.key, cmd.value);
                return true;
            });

        dispatcher.registerQueryHandler<::events::save::GetConfigStringQuery>(
            [this](const ::events::save::GetConfigStringQuery& q)
            {
                return getString(q.key, q.defaultValue);
            });

        dispatcher.registerCommandHandler<::events::save::SetConfigBoolCommand>(
            [this](const ::events::save::SetConfigBoolCommand& cmd)
            {
                setBool(cmd.key, cmd.value);
                return true;
            });

        dispatcher.registerQueryHandler<::events::save::GetConfigBoolQuery>(
            [this](const ::events::save::GetConfigBoolQuery& q)
            {
                return getBool(q.key, q.defaultValue);
            });
    }

    void ConfigService::setInt(const std::string& key, int64_t value)
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        configData[key] = value;
        flush();
    }

    int64_t ConfigService::getInt(const std::string& key, int64_t defaultValue) const
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        if (configData.contains(key) && configData[key].is_number_integer())
        {
            return configData[key].get<int64_t>();
        }
        return defaultValue;
    }

    void ConfigService::setFloat(const std::string& key, double value)
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        configData[key] = value;
        flush();
    }

    double ConfigService::getFloat(const std::string& key, double defaultValue) const
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        // is_number_float() matches only float JSON values, not integers
        if (configData.contains(key) && configData[key].is_number_float())
        {
            return configData[key].get<double>();
        }
        return defaultValue;
    }

    void ConfigService::setString(const std::string& key, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        configData[key] = value;
        flush();
    }

    std::string ConfigService::getString(const std::string& key, const std::string& defaultValue) const
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        if (configData.contains(key) && configData[key].is_string())
        {
            return configData[key].get<std::string>();
        }
        return defaultValue;
    }

    void ConfigService::setBool(const std::string& key, bool value)
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        configData[key] = value;
        flush();
    }

    bool ConfigService::getBool(const std::string& key, bool defaultValue) const
    {
        std::lock_guard<std::mutex> lock(configMutex);
        ensureLoaded();
        if (configData.contains(key) && configData[key].is_boolean())
        {
            return configData[key].get<bool>();
        }
        return defaultValue;
    }

    void ConfigService::ensureLoaded() const
    {
        std::string path = getConfigPath();
        if (loaded && path == loadedPath) return;

        loaded = true;
        loadedPath = path;
        configData = json::object();

        if (path.empty() || !std::filesystem::exists(path)) return;

        try
        {
            configData = resource::readJsonFile(path);
            if (configData.is_null()) configData = json::object();
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[ConfigService] Failed to load config: {}", e.what());
            configData = json::object();
        }
    }

    void ConfigService::flush()
    {
        std::string path = getConfigPath();
        if (path.empty()) return;

        try
        {
            std::ofstream file(path);
            file << configData.dump(2);
            file.close();
        }
        catch (const std::exception& e)
        {
            vfLogError("[ConfigService] Failed to write config: {}", e.what());
        }
    }

    std::string ConfigService::getConfigPath() const
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::project::GetProjectPathQuery pathQuery;
        auto projectPath = dispatcher.query(pathQuery);

        if (projectPath.has_value())
        {
            // GetProjectPathQuery returns the .vfproj FILE, not its folder — appending
            // "/config.json" to it produced "<project>/MyGame.vfproj/config.json", which no
            // directory ever contains, so every Config:: read fell back to its default.
            return (std::filesystem::path(projectPath.value()).parent_path() / "config.json").generic_string();
        }
        return "";
    }
}
