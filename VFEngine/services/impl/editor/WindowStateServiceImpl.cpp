#include "WindowStateServiceImpl.hpp"
#include "../../Window/controllers/WindowStateController.hpp"
#include "../../Window/window/Window.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/ApplicationEvents.hpp"

namespace services {

    WindowStateServiceImpl::WindowStateServiceImpl(window::Window* window)
        : window(window)
        , windowStateController(std::make_unique<window::WindowStateController>(window)) {}

    WindowStateServiceImpl::~WindowStateServiceImpl() = default;

    uint32_t WindowStateServiceImpl::getWidth() const {
        if (!windowStateController) return 0;
        return windowStateController->getWindowWidth();
    }

    uint32_t WindowStateServiceImpl::getHeight() const {
        if (!windowStateController) return 0;
        return windowStateController->getWindowHeight();
    }

    bool WindowStateServiceImpl::isMinimized() const {
        if (!windowStateController) return false;
        return windowStateController->isWindowMinimized();
    }

    bool WindowStateServiceImpl::isFocused() const {
        if (!windowStateController) return true;
        return windowStateController->isWindowFocused();
    }

    void WindowStateServiceImpl::update() {
        if (!windowStateController) return;

        auto& dispatcher = events::EventDispatcher::instance();

        // Check for window resize
        if (windowStateController->isWindowResized()) {
            events::application::WindowResizedNotification notification;
            notification.width = windowStateController->getWindowWidth();
            notification.height = windowStateController->getWindowHeight();
            dispatcher.publish(notification);
            windowStateController->resetResizeFlag();
        }

        // Check for minimize state changes
        if (windowStateController->hasMinimizeStateChanged()) {
            if (windowStateController->isWindowMinimized()) {
                events::application::WindowMinimizedNotification notification;
                dispatcher.publish(notification);
            } else {
                events::application::WindowRestoredNotification notification;
                dispatcher.publish(notification);
            }
            windowStateController->resetMinimizeStateChanged();
        }

        // Check for focus state changes
        if (windowStateController->hasFocusStateChanged()) {
            events::application::WindowFocusedNotification notification;
            notification.focused = windowStateController->isWindowFocused();
            dispatcher.publish(notification);
            windowStateController->resetFocusStateChanged();
        }
    }

    void WindowStateServiceImpl::requestClose() {
        if (window) {
            window->closeWindow();
        }

        // Publish notification
        events::application::CloseRequestedNotification notification;
        events::EventDispatcher::instance().publish(notification);
    }

    void WindowStateServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Command handlers
        dispatcher.registerCommandHandler<events::application::CloseCommand>(
            [this](const events::application::CloseCommand&) {
                requestClose();
            });
    }

}
