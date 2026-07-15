#pragma once
#include "../../events/EventDispatcher.hpp"
#include "config/EditorPreferences.hpp"
#include <mutex>

namespace services
{
    class EditorSettingsService
    {
    private:
        config::EditorPreferences currentSettings;
        mutable std::mutex settingsMutex;
        mutable bool loaded = false;

    public:
        EditorSettingsService();
        ~EditorSettingsService() = default;

        void registerEventHandlers(::events::EventDispatcher& dispatcher);

    private:
        void ensureLoaded();
        void applyRuntimeSettings();
        // Separate from applyRuntimeSettings: the log level is owned outright by the
        // preference, but the Master bus is shared with scripts and mix snapshots, so the
        // preference may only assert onto it when the preference itself changes.
        void applyGlobalMute();
        void save();
        void notifySettingsChanged();
        std::string getSettingsPath() const;
    };
}
