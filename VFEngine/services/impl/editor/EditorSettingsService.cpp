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
                // Captured AFTER ensureLoaded and BEFORE the assignment below: ensureLoaded
                // overwrites currentSettings from disk, so reading the old mute any earlier
                // compares against a value that is about to be thrown away.
                const bool wasMuted = currentSettings.audio.globalMuted;
                currentSettings = cmd.settings;
                applyRuntimeSettings();
                // Only when the PREFERENCE changed. The Master bus is shared — a mix
                // snapshot or a script may own its mute right now — so re-asserting a
                // value the user did not touch, every time an unrelated preference is
                // saved, silently un-mutes audio something else deliberately silenced.
                if (currentSettings.audio.globalMuted != wasMuted)
                    applyGlobalMute();
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
                // Unconditional, deliberately — this is the ONE command that is about the
                // mute, so the user pressing the toolbar toggle must always reach the bus.
                // EngineToolbar reads the LIVE bus to draw itself but writes the PREFERENCE,
                // so the two can legitimately disagree: if a script muted Master while the
                // preference already said muted, a change-gate here would make the unmute
                // button do nothing at all.
                applyGlobalMute();
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
                const bool wasMuted = currentSettings.audio.globalMuted;
                currentSettings = config::EditorPreferences::createDefault();
                applyRuntimeSettings();
                // Same rule as SetEditorSettingsCommand: resetting preferences is not a
                // statement about a Master mute somebody else owns.
                if (currentSettings.audio.globalMuted != wasMuted)
                    applyGlobalMute();
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

        // Startup is the one place the preference legitimately asserts onto the Master bus:
        // nothing else has had a chance to speak for it yet, so there is nothing to clobber.
        const std::string path = getSettingsPath();
        if (path.empty())
        {
            applyRuntimeSettings();
            applyGlobalMute();
            return;
        }

        if (!std::filesystem::exists(path))
        {
            save();
            applyRuntimeSettings();
            applyGlobalMute();
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
        applyGlobalMute();
    }

    void EditorSettingsService::applyRuntimeSettings()
    {
        applyLogLevel(currentSettings.debug.logLevel);
    }

    void EditorSettingsService::applyGlobalMute()
    {
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
