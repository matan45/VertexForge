#pragma once
#include <memory>

// Forward declarations for services provider interfaces
namespace services {
    class IOffScreenProvider;
    class IEditorTextureProvider;
    class IPreviewProvider;
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
    class EditorTextureAdapter;
    class PreviewAdapter;

    /**
     * @brief Bootstrap class for Editor initialization.
     *
     * This class owns the Core and Graphics layer controllers and provides
     * service provider interfaces to the Editor layer. This allows Editor
     * to use engine functionality without direct dependencies on Core.
     */
    class EditorBootstrap {
    public:
        EditorBootstrap();
        ~EditorBootstrap();

        // Non-copyable
        EditorBootstrap(const EditorBootstrap&) = delete;
        EditorBootstrap& operator=(const EditorBootstrap&) = delete;

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

        /**
         * @brief Get the editor texture provider.
         * Valid after init() is called.
         */
        services::IEditorTextureProvider* getEditorTextureProvider();

        /**
         * @brief Get the preview rendering provider.
         * Valid after init() is called.
         */
        services::IPreviewProvider* getPreviewProvider();

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
        std::unique_ptr<EditorTextureAdapter> textureAdapter;
        std::unique_ptr<PreviewAdapter> previewAdapter;
    };

}
