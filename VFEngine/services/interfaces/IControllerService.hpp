#pragma once

namespace services {

    class IControllerService {
    public:
        virtual ~IControllerService() = default;

        virtual void registerEventHandlers() = 0;

        // Called each frame in play mode to apply movement from all controllers
        virtual void applyControllerMovement(float deltaTime) = 0;
    };

}
