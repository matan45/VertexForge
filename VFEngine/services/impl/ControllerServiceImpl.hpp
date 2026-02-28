#pragma once
#include "../interfaces/IControllerService.hpp"

namespace services {

    class ControllerServiceImpl : public IControllerService {
    public:
        void registerEventHandlers() override;
        void applyControllerMovement(float deltaTime) override;
    };

}
