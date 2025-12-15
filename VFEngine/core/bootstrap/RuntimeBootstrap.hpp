#pragma once
#include <memory>

// Forward declarations for services provider interfaces
namespace services {
    class IOffScreenProvider;
}

// Forward declarations for window interfaces
namespace window {
    class Window;
}

namespace scene {
    class SceneGraphSystem;
}

// Forward declarations for Core controllers (global namespace)
namespace controllers {
    class CoreInterface;
    class OffScreen;
}

namespace core {

    // Forward declarations for internal classes
    class OffScreenAdapter;

    /**
     * @brief Bootstrap class for Runtime initialization.
     *
     * This class owns the Core and Graphics layer controllers and provides
     * service provider interfaces to the Runtime layer. This allows Runtime
     * to use engine functionality without direct dependencies on Core.
     *
     * Unlike EditorBootstrap, this does not include editor-specific features
     * like texture loading for UI or preview rendering.
     */
    class RuntimeBootstrap {
    public:
        RuntimeBootstrap();
        ~RuntimeBootstrap();

        // Non-copyable
        RuntimeBootstrap(const RuntimeBootstrap&) = delete;
        RuntimeBootstrap& operator=(const RuntimeBootstrap&) = delete;

        /**
         * @brief Initialize the engine core systems.
         */
        void init();

        /**
         * @brief Run the main engine loop.
         */
        void run() const;

        /**
         * @brief Clean up all engine systems.
         */
        void cleanUp();

        // === Provider Accessors ===

        /**
         * @brief Get the offscreen rendering provider.
         * Valid after init() is called.
         */
        services::IOffScreenProvider* getOffScreenProvider();

        // === Other Accessors ===

        /**
         * @brief Get the window for InputService initialization.
         * Valid after init() is called.
         */
        window::Window* getWindow();

        /**
         * @brief Get the scene graph system for SceneService.
         * Valid after init() is called.
         */
        std::shared_ptr<scene::SceneGraphSystem> getSceneGraphSystem();

    private:
        // Core layer controllers
        std::unique_ptr<::controllers::CoreInterface> coreInterface;
        std::unique_ptr<::controllers::OffScreen> offScreen;

        // Adapters that implement provider interfaces
        std::unique_ptr<OffScreenAdapter> offScreenAdapter;
    };

}
