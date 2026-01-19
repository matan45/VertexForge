#include "AnimatorSystemController.hpp"
#include "../animation/RuntimeAnimatorSystem.hpp"

namespace controllers
{
    void AnimatorSystemController::init()
    {
        animation::RuntimeAnimatorSystem::instance().initialize();
    }

    void AnimatorSystemController::cleanUp()
    {
        animation::RuntimeAnimatorSystem::instance().shutdown();
    }
}
