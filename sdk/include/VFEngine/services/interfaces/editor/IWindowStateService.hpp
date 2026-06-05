#pragma once
#include <cstdint>

namespace services {

    // Window state service interface - abstracts window state handling
    // Presentation layer uses this instead of direct Window access
    class IWindowStateService {
    public:
        virtual ~IWindowStateService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // ============================================
        // Window Dimensions
        // ============================================

        virtual uint32_t getWidth() const = 0;
        virtual uint32_t getHeight() const = 0;

        // ============================================
        // Window State
        // ============================================

        virtual bool isMinimized() const = 0;
        virtual bool isFocused() const = 0;

        // ============================================
        // State Management
        // ============================================

        // Update window state and publish notifications (called once per frame)
        virtual void update() = 0;

        // ============================================
        // Application Control
        // ============================================

        // Request the application to close
        virtual void requestClose() = 0;
    };

}
