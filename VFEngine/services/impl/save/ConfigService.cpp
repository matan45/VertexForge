#include "ConfigService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/save/ConfigEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "print/Log.hpp"
#include "resource/VFSHelpers.hpp"
#include <fstream>
#include <filesystem>
#include <system_error>
#include <utility>

namespace services
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    ConfigService::ConfigService(std::filesystem::path stateFolder)
        : stateFolder(std::move(stateFolder))
    {
        configData = json::object();
    }

    ConfigService::~ConfigService()
    {
        // The dispatcher keys handlers by type: an instance that never registered must not remove another one's.
        if (!handlersRegistered)
        {
            return;
        }
        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<::events::save::SetConfigIntCommand>();
        dispatcher.unregisterQueryHandler<::events::save::GetConfigIntQuery>();
        dispatcher.unregisterCommandHandler<::events::save::SetConfigFloatCommand>();
        dispatcher.unregisterQueryHandler<::events::save::GetConfigFloatQuery>();
        dispatcher.unregisterCommandHandler<::events::save::SetConfigStringCommand>();
        dispatcher.unregisterQueryHandler<::events::save::GetConfigStringQuery>();
        dispatcher.unregisterCommandHandler<::events::save::SetConfigBoolCommand>();
        dispatcher.unregisterQueryHandler<::events::save::GetConfigBoolQuery>();
    }

    void ConfigService::registerEventHandlers(::events::EventDispatcher& dispatcher)
    {
        handlersRegistered = true;

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
        if (loaded) return;
        loaded = true;

        std::string path = getConfigPath();
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
            // The editor's state folder may not exist yet; a failure shows in the write below.
            std::error_code ec;
            fs::create_directories(fs::path(path).parent_path(), ec);

            std::ofstream file(path);
            file << configData.dump(2);
            file.close();
            if (!file)
            {
                // Before VK-1726 a game installed under Program Files lost every setting here, silently.
                vfLogError("[ConfigService] Failed to write {}", path);
            }
        }
        catch (const std::exception& e)
        {
            vfLogError("[ConfigService] Failed to write config: {}", e.what());
        }
    }

    std::string ConfigService::getConfigPath() const
    {
        // VK-1726: an absolute state folder (the Runtime's) holds config.json whether a project is open or not.
        if (stateFolder.is_absolute())
        {
            return (stateFolder / "config.json").string();
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::project::GetProjectPathQuery pathQuery;
        auto projectPath = dispatcher.query(pathQuery);

        if (projectPath.has_value() && !projectPath->empty())
        {
            // The query answers the .vfproj FILE path (as AssetDatabase reads it): config.json lives
            // in its folder, under stateFolder.
            fs::path folder = fs::path(*projectPath).parent_path();
            if (!stateFolder.empty())
            {
                folder /= stateFolder;
            }
            return (folder / "config.json").string();
        }
        return "";
    }
}
