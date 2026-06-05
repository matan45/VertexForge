#pragma once
#include "../../interfaces/editor/IWindowStateService.hpp"
#include "../../events/EventDispatcher.hpp"
#include <glm/glm.hpp>
#include <memory>

// Forward declaration - Window library
namespace window {
    class Window;
    class WindowStateController;
}

namespace services {

    class WindowStateServiceImpl : public IWindowStateService {
    public:
        explicit WindowStateServiceImpl(window::Window* window);
        ~WindowStateServiceImpl() override;

        void registerEventHandlers() override;

        // Window Dimensions
        uint32_t getWidth() const override;
        uint32_t getHeight() const override;

        // Window State
        bool isMinimized() const override;
        bool isFocused() const override;

        // State Management
        void update() override;

        // Application Control
        void requestClose() override;

    private:
        window::Window* window;
        std::unique_ptr<window::WindowStateController> windowStateController;

        glm::vec2 playViewportOffset{0.0f, 0.0f};
        glm::vec2 playViewportSize{0.0f, 0.0f};
        events::ScopedSubscription playViewportSub;
    };

}
