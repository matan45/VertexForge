#include "AnimatorComponentService.hpp"
#include "../../providers/animation/IAnimatorProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/animation/AnimatorEvents.hpp"
#include "../../events/animation/AnimationBudgetEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"

namespace services
{
    AnimatorComponentService::AnimatorComponentService(IAnimatorProvider* provider)
        : animatorProvider(provider)
    {
    }

    void AnimatorComponentService::setFloat(EntityHandle entity, const std::string& paramName, float value)
    {
        animatorProvider->setFloat(entity, paramName, value);
    }

    void AnimatorComponentService::setInt(EntityHandle entity, const std::string& paramName, int32_t value)
    {
        animatorProvider->setInt(entity, paramName, value);
    }

    void AnimatorComponentService::setBool(EntityHandle entity, const std::string& paramName, bool value)
    {
        animatorProvider->setBool(entity, paramName, value);
    }

    void AnimatorComponentService::setTrigger(EntityHandle entity, const std::string& paramName)
    {
        animatorProvider->setTrigger(entity, paramName);
    }

    float AnimatorComponentService::getFloat(EntityHandle entity, const std::string& paramName) const
    {
        return animatorProvider->getFloat(entity, paramName);
    }

    int32_t AnimatorComponentService::getInt(EntityHandle entity, const std::string& paramName) const
    {
        return animatorProvider->getInt(entity, paramName);
    }

    bool AnimatorComponentService::getBool(EntityHandle entity, const std::string& paramName) const
    {
        return animatorProvider->getBool(entity, paramName);
    }

    void AnimatorComponentService::play(EntityHandle entity)
    {
        animatorProvider->play(entity);
    }

    void AnimatorComponentService::pause(EntityHandle entity)
    {
        animatorProvider->pause(entity);
    }

    void AnimatorComponentService::stop(EntityHandle entity)
    {
        animatorProvider->stop(entity);
    }

    void AnimatorComponentService::reset(EntityHandle entity)
    {
        animatorProvider->reset(entity);
    }

    bool AnimatorComponentService::isPlaying(EntityHandle entity) const
    {
        return animatorProvider->isPlaying(entity);
    }

    bool AnimatorComponentService::isBlending(EntityHandle entity) const
    {
        return animatorProvider->isBlending(entity);
    }

    std::string AnimatorComponentService::getCurrentState(EntityHandle entity) const
    {
        return animatorProvider->getCurrentState(entity);
    }

    float AnimatorComponentService::getNormalizedTime(EntityHandle entity) const
    {
        return animatorProvider->getNormalizedTime(entity);
    }

    bool AnimatorComponentService::hasAnimator(EntityHandle entity) const
    {
        return animatorProvider->hasAnimator(entity);
    }

    bool AnimatorComponentService::forceTransitionTo(EntityHandle entity, const std::string& stateName,
                                                     float blendDuration)
    {
        return animatorProvider->forceTransitionTo(entity, stateName, blendDuration);
    }

    void AnimatorComponentService::setRootMotion(EntityHandle entity, bool enabled)
    {
        animatorProvider->setRootMotion(entity, enabled);
    }

    bool AnimatorComponentService::getRootMotion(EntityHandle entity) const
    {
        return animatorProvider->getRootMotion(entity);
    }

    void AnimatorComponentService::registerEventHandlers(::events::EventDispatcher& dispatcher)
    {
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

        dispatcher.registerCommandHandler<events::animator::ForceEntityTransitionToCommand>(
            [this](const events::animator::ForceEntityTransitionToCommand& cmd)
            {
                return forceTransitionTo(cmd.entity, cmd.stateName, cmd.blendDuration);
            });

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

        dispatcher.registerCommandHandler<events::animator::SetEntityRootMotionCommand>(
            [this](const events::animator::SetEntityRootMotionCommand& cmd)
            {
                setRootMotion(cmd.entity, cmd.enabled);
            });

        dispatcher.registerQueryHandler<events::animator::GetEntityRootMotionQuery>(
            [this](const events::animator::GetEntityRootMotionQuery& query)
            {
                return getRootMotion(query.entity);
            });

        dispatcher.registerQueryHandler<services::events::animation::GetAnimationBudgetStatsQuery>(
            [this](const services::events::animation::GetAnimationBudgetStatsQuery&)
            {
                auto ps = animatorProvider->getBudgetStats();
                services::events::animation::AnimationBudgetStatsResult result;
                result.totalAnimators = ps.totalAnimators;
                result.culledEntities = ps.culledEntities;
                for (int i = 0; i < 4; ++i)
                    result.lodCounts[i] = ps.lodCounts[i];
                result.pendingStreamingInits = ps.pendingStreamingInits;
                return result;
            });

        dispatcher.registerQueryHandler<services::events::animation::GetAnimationLODConfigQuery>(
            [this](const services::events::animation::GetAnimationLODConfigQuery&)
            {
                auto lc = animatorProvider->getLODConfig();
                services::events::animation::AnimationLODConfigResult result;
                result.lod0Distance = lc.lod0Distance;
                result.lod1Distance = lc.lod1Distance;
                result.lod2Distance = lc.lod2Distance;
                result.lod3Distance = lc.lod3Distance;
                result.lod0Interval = lc.lod0Interval;
                result.lod1Interval = lc.lod1Interval;
                result.lod2Interval = lc.lod2Interval;
                result.maxStreamingInitPerFrame = lc.maxStreamingInitPerFrame;
                return result;
            });

        dispatcher.registerCommandHandler<services::events::animation::SetAnimationLODConfigCommand>(
            [this](const services::events::animation::SetAnimationLODConfigCommand& cmd)
            {
                services::IAnimatorProvider::LODConfig config;
                config.lod0Distance = cmd.lod0Distance;
                config.lod1Distance = cmd.lod1Distance;
                config.lod2Distance = cmd.lod2Distance;
                config.lod3Distance = cmd.lod3Distance;
                config.lod0Interval = cmd.lod0Interval;
                config.lod1Interval = cmd.lod1Interval;
                config.lod2Interval = cmd.lod2Interval;
                config.maxStreamingInitPerFrame = cmd.maxStreamingInitPerFrame;
                animatorProvider->setLODConfig(config);
            });

        // Layer management
        dispatcher.registerCommandHandler<events::animator::SetAnimationLayerWeightCommand>(
            [this](const events::animator::SetAnimationLayerWeightCommand& cmd)
            {
                animatorProvider->setLayerWeight(cmd.entity, cmd.layerIndex, cmd.weight);
            });

        dispatcher.registerQueryHandler<events::animator::GetAnimationLayerWeightQuery>(
            [this](const events::animator::GetAnimationLayerWeightQuery& query)
            {
                return animatorProvider->getLayerWeight(query.entity, query.layerIndex);
            });

        dispatcher.registerQueryHandler<events::animator::GetAnimationLayerCountQuery>(
            [this](const events::animator::GetAnimationLayerCountQuery& query)
            {
                return animatorProvider->getLayerCount(query.entity);
            });

        dispatcher.registerQueryHandler<events::animator::GetAnimationLayerNameQuery>(
            [this](const events::animator::GetAnimationLayerNameQuery& query)
            {
                return animatorProvider->getLayerName(query.entity, query.layerIndex);
            });

        dispatcher.registerQueryHandler<events::animator::GetAnimatorRuntimeDebugDataQuery>(
            [this](const events::animator::GetAnimatorRuntimeDebugDataQuery& query)
            {
                return animatorProvider->getDebugData(query.entity, query.layerIndex);
            });

        dispatcher.subscribe<::events::scene::SceneLoadedNotification>(
            [this](const ::events::scene::SceneLoadedNotification&)
            {
                auto& disp = ::events::EventDispatcher::instance();
                ::events::scene::GetRenderSettingsQuery query;
                auto settings = disp.query(query);
                const auto& animLOD = settings.animationLOD;

                services::events::animation::SetAnimationLODConfigCommand cmd;
                cmd.lod0Distance = animLOD.lod0Distance;
                cmd.lod1Distance = animLOD.lod1Distance;
                cmd.lod2Distance = animLOD.lod2Distance;
                cmd.lod3Distance = animLOD.lod3Distance;
                cmd.lod0Interval = animLOD.lod0Interval;
                cmd.lod1Interval = animLOD.lod1Interval;
                cmd.lod2Interval = animLOD.lod2Interval;
                cmd.maxStreamingInitPerFrame = animLOD.maxStreamingInitPerFrame;
                disp.execute(cmd);
            });
    }
}
