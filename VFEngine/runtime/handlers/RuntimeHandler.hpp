#pragma once
#include <memory>

// Service interfaces
#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IInputService.hpp"

// Forward declaration for RuntimeBootstrap
namespace core {
    class RuntimeBootstrap;
}

namespace handlers {

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

        // Bootstrap encapsulates Core/Graphics initialization
        std::unique_ptr<core::RuntimeBootstrap> bootstrap;

        // Service implementations (stored to keep them alive)
        std::shared_ptr<services::ISceneService> sceneService;
        std::shared_ptr<services::IRenderService> renderService;
        std::shared_ptr<services::IInputService> inputService;
    };

}
