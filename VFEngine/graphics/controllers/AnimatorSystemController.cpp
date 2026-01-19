#include "AnimatorSystemController.hpp"
#include "../animation/RuntimeAnimatorSystem.hpp"
#include "../animation/AnimatorStateMachine.hpp"

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

    void AnimatorSystemController::setFloat(entt::entity entity, const std::string& paramName, float value)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setFloat(paramName, value);
    }

    void AnimatorSystemController::setInt(entt::entity entity, const std::string& paramName, int32_t value)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setInt(paramName, value);
    }

    void AnimatorSystemController::setBool(entt::entity entity, const std::string& paramName, bool value)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setBool(paramName, value);
    }

    void AnimatorSystemController::setTrigger(entt::entity entity, const std::string& paramName)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setTrigger(paramName);
    }

    float AnimatorSystemController::getFloat(entt::entity entity, const std::string& paramName) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getFloat(paramName);
        return 0.0f;
    }

    int32_t AnimatorSystemController::getInt(entt::entity entity, const std::string& paramName) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getInt(paramName);
        return 0;
    }

    bool AnimatorSystemController::getBool(entt::entity entity, const std::string& paramName) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getBool(paramName);
        return false;
    }

    void AnimatorSystemController::play(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->play();
    }

    void AnimatorSystemController::pause(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->pause();
    }

    void AnimatorSystemController::stop(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->stop();
    }

    void AnimatorSystemController::reset(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->reset();
    }

    bool AnimatorSystemController::isPlaying(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->isPlaying();
        return false;
    }

    bool AnimatorSystemController::isBlending(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->isBlending();
        return false;
    }

    std::string AnimatorSystemController::getCurrentState(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator && animator->isInitialized())
        {
            auto* currentState = animator->getCurrentAnimatorState();
            if (currentState)
                return currentState->name;
        }
        return "";
    }

    float AnimatorSystemController::getNormalizedTime(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getNormalizedStateTime();
        return 0.0f;
    }

    bool AnimatorSystemController::hasAnimator(entt::entity entity) const
    {
        return animation::RuntimeAnimatorSystem::instance().hasAnimator(entity);
    }

    bool AnimatorSystemController::forceTransitionTo(entt::entity entity, const std::string& stateName,
                                                     float blendDuration)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
        {
            animator->forceTransitionTo(stateName, blendDuration);
            return true;
        }
        return false;
    }
}
