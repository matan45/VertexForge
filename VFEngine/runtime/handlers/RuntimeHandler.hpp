#pragma once
#include <memory>

#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IInputService.hpp"
#include "interfaces/IWindowStateService.hpp"
#include "events/EventTypes.hpp"

namespace core {
    class RuntimeBootstrap;
}

namespace handlers {

    class RuntimeHandler {
    private:
        std::unique_ptr<core::RuntimeBootstrap> bootstrap;

        std::shared_ptr<services::ISceneService> sceneService;
        std::shared_ptr<services::IRenderService> renderService;
        std::shared_ptr<services::IInputService> inputService;
        std::shared_ptr<services::IWindowStateService> windowStateService;

        events::SubscriptionToken resizeSubscription;
        events::SubscriptionToken minimizeSubscription;
        events::SubscriptionToken restoreSubscription;

    public:
        explicit RuntimeHandler();
        ~RuntimeHandler();

        // Non-copyable
        RuntimeHandler(const RuntimeHandler&) = delete;
        RuntimeHandler& operator=(const RuntimeHandler&) = delete;

        void init();
        void run() const;
        void cleanUp();
        bool loadProject(const std::string& projectPath);

    private:
        void initializeServices();
        void setupEventSubscriptions();
        void cleanupEventSubscriptions();
    };

}
