#include "EditorSettingsService.hpp"
#include "../../events/editor/EditorSettingsEvents.hpp"
#include "../../events/audio/AudioBusEvents.hpp"
#include "config/EditorPreferencesSerializer.hpp"
#include "print/Log.hpp"
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace services
{
    using json = nlohmann::json;

    namespace
    {
        void applyLogLevel(const std::string& level)
        {
            if (level == "Trace") util::minLogLevel = util::LogLevel::Trace;
            else if (level == "Debug") util::minLogLevel = util::LogLevel::Debug;
            else if (level == "Info") util::minLogLevel = util::LogLevel::Info;
            else if (level == "Warning") util::minLogLevel = util::LogLevel::Warning;
            else if (level == "Error") util::minLogLevel = util::LogLevel::Error;
        }
    }

    EditorSettingsService::EditorSettingsService()
    {
        currentSettings = config::EditorPreferences::createDefault();
    }

    void EditorSettingsService::registerEventHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<::events::editor::SetEditorSettingsCommand>(
            [this](const ::events::editor::SetEditorSettingsCommand& cmd)
            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                ensureLoaded();
                currentSettings = cmd.settings;
                applyRuntimeSettings();
                save();
                notifySettingsChanged();
                return true;
            });

        dispatcher.registerCommandHandler<::events::editor::SetEditorAudioMutedCommand>(
            [this](const ::events::editor::SetEditorAudioMutedCommand& cmd)
            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                ensureLoaded();
                currentSettings.audio.globalMuted = cmd.muted;
                applyRuntimeSettings();
                save();
                notifySettingsChanged();
                return true;
            });

        dispatcher.registerCommandHandler<::events::editor::SaveEditorSettingsCommand>(
            [this](const ::events::editor::SaveEditorSettingsCommand&)
            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                ensureLoaded();
                save();
                return true;
            });

        dispatcher.registerCommandHandler<::events::editor::ResetEditorSettingsCommand>(
            [this](const ::events::editor::ResetEditorSettingsCommand&)
            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                currentSettings = config::EditorPreferences::createDefault();
                applyRuntimeSettings();
                save();
                notifySettingsChanged();
                return true;
            });

        dispatcher.registerQueryHandler<::events::editor::GetEditorSettingsQuery>(
            [this](const ::events::editor::GetEditorSettingsQuery&)
            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                ensureLoaded();
                return currentSettings;
            });

        dispatcher.registerQueryHandler<::events::editor::GetEditorSettingsPathQuery>(
            [this](const ::events::editor::GetEditorSettingsPathQuery&)
            {
                return getSettingsPath();
            });

        // Audio handlers are registered before editor settings during bootstrap, so
        // applying persisted runtime preferences here is safe and does not depend on UI.
        std::lock_guard<std::mutex> lock(settingsMutex);
        ensureLoaded();
    }

    void EditorSettingsService::ensureLoaded()
    {
        if (loaded) return;
        loaded = true;

        const std::string path = getSettingsPath();
        if (path.empty())
        {
            applyRuntimeSettings();
            return;
        }

        if (!std::filesystem::exists(path))
        {
            save();
            applyRuntimeSettings();
            return;
        }

        try
        {
            std::ifstream file(path);
            if (!file.is_open()) return;

            json j = json::parse(file);
            file.close();

            if (!j.is_null())
                currentSettings = j.get<config::EditorPreferences>();
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[EditorSettingsService] Failed to load settings: {}", e.what());
            currentSettings = config::EditorPreferences::createDefault();
        }

        applyRuntimeSettings();
    }

    void EditorSettingsService::applyRuntimeSettings()
    {
        applyLogLevel(currentSettings.debug.logLevel);

        ::events::audio::SetBusMutedCommand muteCommand;
        muteCommand.busName = "Master";
        muteCommand.muted = currentSettings.audio.globalMuted;
        ::events::EventDispatcher::instance().execute(muteCommand);
    }

    void EditorSettingsService::save()
    {
        std::string path = getSettingsPath();
        if (path.empty()) return;

        try
        {
            auto parentDir = std::filesystem::path(path).parent_path();
            if (!std::filesystem::exists(parentDir))
                std::filesystem::create_directories(parentDir);

            json j = currentSettings;
            std::ofstream file(path);
            file << j.dump(2);
            file.close();
        }
        catch (const std::exception& e)
        {
            vfLogError("[EditorSettingsService] Failed to save settings: {}", e.what());
        }
    }

    void EditorSettingsService::notifySettingsChanged()
    {
        ::events::editor::EditorSettingsChangedNotification notification;
        notification.settings = currentSettings;
        ::events::EventDispatcher::instance().publish(notification);
    }

    std::string EditorSettingsService::getSettingsPath() const
    {
        char* home = nullptr;
        size_t len = 0;
        _dupenv_s(&home, &len, "USERPROFILE");
        if (!home)
            _dupenv_s(&home, &len, "HOME");
        if (!home)
            return "";
        std::string result = std::string(home) + "/.vertexforge/editor_settings.json";
        free(home);
        return result;
    }
}
