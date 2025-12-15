#pragma once
#include <memory>

// Service interfaces
#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IInputService.hpp"
#include "providers/IEditorTextureProvider.hpp"
#include "events/EventTypes.hpp"

// Forward declaration for RuntimeBootstrap
namespace core {
    class RuntimeBootstrap;
}

namespace handlers {

    /**
     * @brief Null implementation of IEditorTextureProvider for Runtime.
     *
     * Runtime doesn't need editor texture loading, so this provides a no-op implementation.
     */
    class NullEditorTextureProvider : public services::IEditorTextureProvider {
    public:
        services::EditorTextureData loadTexture(std::string_view) override {
            return services::EditorTextureData{};
        }

        services::EditorTextureData loadHdrTexture(std::string_view) override {
            return services::EditorTextureData{};
        }

        void releaseTexture(void*) override {}
    };

    /**
     * @brief Handler for standalone runtime game execution.
     *
     * This class manages the runtime game loop and initializes services
     * through the RuntimeBootstrap, without any Editor or Import dependencies.
     */
    class RuntimeHandler {
    public:
        RuntimeHandler();
        ~RuntimeHandler();

        // Non-copyable
        RuntimeHandler(const RuntimeHandler&) = delete;
        RuntimeHandler& operator=(const RuntimeHandler&) = delete;

        /**
         * @brief Initialize the runtime systems.
         */
        void init();

        /**
         * @brief Run the main game loop.
         */
        void run() const;

        /**
         * @brief Clean up all runtime systems.
         */
        void cleanUp();

        /**
         * @brief Load a scene from file.
         * @param scenePath Path to the scene file
         * @return true if scene loaded successfully
         */
        bool loadScene(const std::string& scenePath);

    private:
        void initializeServices();
        void setupEventSubscriptions();
        void cleanupEventSubscriptions();

        // Bootstrap encapsulates Core/Graphics initialization
        std::unique_ptr<core::RuntimeBootstrap> bootstrap;

        // Null texture provider for Runtime (no editor textures needed)
        // Declared before services to ensure correct destruction order
        NullEditorTextureProvider nullTextureProvider;

        // Service implementations (stored to keep them alive)
        // These are destroyed before nullTextureProvider due to declaration order
        std::shared_ptr<services::ISceneService> sceneService;
        std::shared_ptr<services::IRenderService> renderService;
        std::shared_ptr<services::IInputService> inputService;

        // Event subscription tokens
        events::SubscriptionToken resizeSubscription;
        events::SubscriptionToken minimizeSubscription;
        events::SubscriptionToken restoreSubscription;
    };

}
