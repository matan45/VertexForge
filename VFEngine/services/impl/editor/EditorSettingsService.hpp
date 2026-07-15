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
        void save();
        void notifySettingsChanged();
        std::string getSettingsPath() const;
    };
}
