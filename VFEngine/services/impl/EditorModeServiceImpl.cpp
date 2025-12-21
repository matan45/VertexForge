#include "EditorModeServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/EditorModeEvents.hpp"

namespace services {

    void EditorModeServiceImpl::setMode(EditorMode mode) {
        if (currentMode == mode) {
            return;
        }

        EditorMode previousMode = currentMode;
        currentMode = mode;

        // Publish notification
        events::editor::EditorModeChangedNotification notification;
        notification.previousMode = previousMode;
        notification.currentMode = currentMode;
        events::EventDispatcher::instance().publish(notification);
    }

    EditorMode EditorModeServiceImpl::getMode() const {
        return currentMode;
    }

    bool EditorModeServiceImpl::isPlayMode() const {
        return currentMode == EditorMode::Play;
    }

    bool EditorModeServiceImpl::isEditMode() const {
        return currentMode == EditorMode::Edit;
    }

    bool EditorModeServiceImpl::isPauseMode() const {
        return currentMode == EditorMode::Pause;
    }

    void EditorModeServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Command handlers
        dispatcher.registerCommandHandler<events::editor::SetEditorModeCommand>(
            [this](const events::editor::SetEditorModeCommand& cmd) {
                setMode(cmd.mode);
            });

        // Query handlers
        dispatcher.registerQueryHandler<events::editor::GetEditorModeQuery>(
            [this](const events::editor::GetEditorModeQuery&) {
                return getMode();
            });

        dispatcher.registerQueryHandler<events::editor::IsPlayModeQuery>(
            [this](const events::editor::IsPlayModeQuery&) {
                return isPlayMode();
            });

        dispatcher.registerQueryHandler<events::editor::IsEditModeQuery>(
            [this](const events::editor::IsEditModeQuery&) {
                return isEditMode();
            });
    }

}
