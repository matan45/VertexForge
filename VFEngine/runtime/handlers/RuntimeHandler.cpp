#include "RuntimeHandler.hpp"
#include "RuntimeBootstrap.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "providers/IEditorTextureProvider.hpp"

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

    RuntimeHandler::RuntimeHandler()
        : bootstrap(std::make_unique<core::RuntimeBootstrap>()) {}

    RuntimeHandler::~RuntimeHandler() = default;

    void RuntimeHandler::init() {
        // Initialize core systems via bootstrap
        bootstrap->init();

        // Initialize services with providers from bootstrap
        initializeServices();
    }

    void RuntimeHandler::run() const {
        bootstrap->run();
    }

    void RuntimeHandler::cleanUp() {
        // Reset services before graphics cleanup to release Vulkan resources
        renderService.reset();
        sceneService.reset();
        inputService.reset();

        // Clean up via bootstrap
        bootstrap->cleanUp();
    }

    bool RuntimeHandler::loadScene(const std::string& scenePath) {
        // TODO: Implement scene loading
        // This will load a serialized scene file and populate the ECS
        return false;
    }

    void RuntimeHandler::initializeServices() {
        // Get shared instances from the bootstrap
        auto sceneGraphSystem = bootstrap->getSceneGraphSystem();

        // Create null texture provider for Runtime (no editor textures needed)
        static NullEditorTextureProvider nullTextureProvider;

        // Create service implementations using providers from bootstrap
        auto sceneServiceImpl = std::make_shared<services::SceneServiceImpl>(sceneGraphSystem);
        auto renderServiceImpl = std::make_shared<services::RenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            &nullTextureProvider
        );
        auto inputServiceImpl = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());

        sceneService = sceneServiceImpl;
        renderService = renderServiceImpl;
        inputService = inputServiceImpl;

        // Register event handlers for command/query pattern
        sceneServiceImpl->registerEventHandlers();
        renderServiceImpl->registerEventHandlers();
        inputServiceImpl->registerEventHandlers();
    }

}
