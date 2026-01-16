#include "AnimatorComponentService.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/Components.hpp"
#include "../../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "../../../graphics/animation/AnimatorStateMachine.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/AnimatorEvents.hpp"

namespace services
{
    void AnimatorComponentService::setFloat(EntityHandle entity, const std::string& paramName, float value)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->setFloat(paramName, value);
        }
    }

    void AnimatorComponentService::setInt(EntityHandle entity, const std::string& paramName, int32_t value)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->setInt(paramName, value);
        }
    }

    void AnimatorComponentService::setBool(EntityHandle entity, const std::string& paramName, bool value)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->setBool(paramName, value);
        }
    }

    void AnimatorComponentService::setTrigger(EntityHandle entity, const std::string& paramName)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->setTrigger(paramName);
        }
    }

    float AnimatorComponentService::getFloat(EntityHandle entity, const std::string& paramName) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return 0.0f;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            return animator->getFloat(paramName);
        }
        return 0.0f;
    }

    int32_t AnimatorComponentService::getInt(EntityHandle entity, const std::string& paramName) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return 0;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            return animator->getInt(paramName);
        }
        return 0;
    }

    bool AnimatorComponentService::getBool(EntityHandle entity, const std::string& paramName) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            return animator->getBool(paramName);
        }
        return false;
    }

    void AnimatorComponentService::play(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->play();
        }
    }

    void AnimatorComponentService::pause(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->pause();
        }
    }

    void AnimatorComponentService::stop(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->stop();
        }
    }

    void AnimatorComponentService::reset(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->reset();
        }
    }

    bool AnimatorComponentService::isPlaying(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            return animator->isPlaying();
        }
        return false;
    }

    bool AnimatorComponentService::isBlending(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            return animator->isBlending();
        }
        return false;
    }

    std::string AnimatorComponentService::getCurrentState(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return "";
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator && animator->isInitialized())
        {
            auto* currentState = animator->getCurrentAnimatorState();
            if (currentState)
            {
                return currentState->name;
            }
        }
        return "";
    }

    float AnimatorComponentService::getNormalizedTime(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return 0.0f;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            return animator->getNormalizedStateTime();
        }
        return 0.0f;
    }

    bool AnimatorComponentService::hasAnimator(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        return animation::RuntimeAnimatorSystem::instance().hasAnimator(internal::fromHandle(entity));
    }

    bool AnimatorComponentService::forceTransitionTo(EntityHandle entity, const std::string& stateName, float blendDuration)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(internal::fromHandle(entity));
        if (animator)
        {
            animator->forceTransitionTo(stateName, blendDuration);
            return true;
        }
        return false;
    }

    void AnimatorComponentService::registerEventHandlers(::events::EventDispatcher& dispatcher)
    {
        // Parameter setters
        dispatcher.registerCommandHandler<events::animator::SetEntityAnimatorFloatCommand>(
            [this](const events::animator::SetEntityAnimatorFloatCommand& cmd)
            {
                setFloat(cmd.entity, cmd.parameterName, cmd.value);
            });

        dispatcher.registerCommandHandler<events::animator::SetEntityAnimatorIntCommand>(
            [this](const events::animator::SetEntityAnimatorIntCommand& cmd)
            {
                setInt(cmd.entity, cmd.parameterName, cmd.value);
            });

        dispatcher.registerCommandHandler<events::animator::SetEntityAnimatorBoolCommand>(
            [this](const events::animator::SetEntityAnimatorBoolCommand& cmd)
            {
                setBool(cmd.entity, cmd.parameterName, cmd.value);
            });

        dispatcher.registerCommandHandler<events::animator::SetEntityAnimatorTriggerCommand>(
            [this](const events::animator::SetEntityAnimatorTriggerCommand& cmd)
            {
                setTrigger(cmd.entity, cmd.parameterName);
            });

        // Playback control
        dispatcher.registerCommandHandler<events::animator::PlayEntityAnimatorCommand>(
            [this](const events::animator::PlayEntityAnimatorCommand& cmd)
            {
                play(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::animator::PauseEntityAnimatorCommand>(
            [this](const events::animator::PauseEntityAnimatorCommand& cmd)
            {
                pause(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::animator::StopEntityAnimatorCommand>(
            [this](const events::animator::StopEntityAnimatorCommand& cmd)
            {
                stop(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::animator::ResetEntityAnimatorCommand>(
            [this](const events::animator::ResetEntityAnimatorCommand& cmd)
            {
                reset(cmd.entity);
            });

        // Transition control
        dispatcher.registerCommandHandler<events::animator::ForceEntityTransitionToCommand>(
            [this](const events::animator::ForceEntityTransitionToCommand& cmd)
            {
                return forceTransitionTo(cmd.entity, cmd.stateName, cmd.blendDuration);
            });

        // Parameter getters
        dispatcher.registerQueryHandler<events::animator::GetEntityAnimatorFloatQuery>(
            [this](const events::animator::GetEntityAnimatorFloatQuery& query)
            {
                return getFloat(query.entity, query.parameterName);
            });

        dispatcher.registerQueryHandler<events::animator::GetEntityAnimatorIntQuery>(
            [this](const events::animator::GetEntityAnimatorIntQuery& query)
            {
                return getInt(query.entity, query.parameterName);
            });

        dispatcher.registerQueryHandler<events::animator::GetEntityAnimatorBoolQuery>(
            [this](const events::animator::GetEntityAnimatorBoolQuery& query)
            {
                return getBool(query.entity, query.parameterName);
            });

        // State queries
        dispatcher.registerQueryHandler<events::animator::IsEntityAnimatorPlayingQuery>(
            [this](const events::animator::IsEntityAnimatorPlayingQuery& query)
            {
                return isPlaying(query.entity);
            });

        dispatcher.registerQueryHandler<events::animator::IsEntityAnimatorBlendingQuery>(
            [this](const events::animator::IsEntityAnimatorBlendingQuery& query)
            {
                return isBlending(query.entity);
            });

        dispatcher.registerQueryHandler<events::animator::GetEntityAnimatorCurrentStateQuery>(
            [this](const events::animator::GetEntityAnimatorCurrentStateQuery& query)
            {
                return getCurrentState(query.entity);
            });

        dispatcher.registerQueryHandler<events::animator::GetEntityAnimatorNormalizedTimeQuery>(
            [this](const events::animator::GetEntityAnimatorNormalizedTimeQuery& query)
            {
                return getNormalizedTime(query.entity);
            });

        dispatcher.registerQueryHandler<events::animator::HasEntityAnimatorQuery>(
            [this](const events::animator::HasEntityAnimatorQuery& query)
            {
                return hasAnimator(query.entity);
            });
    }
}
