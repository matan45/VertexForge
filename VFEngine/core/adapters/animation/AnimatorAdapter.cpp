#include "AnimatorAdapter.hpp"
#include "../../graphics/controllers/AnimatorSystemController.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"

namespace core
{
    namespace
    {
        template<typename Func>
        void withEntity(services::EntityHandle entity, Func&& func)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!services::internal::isValidHandle(entity, registry))
                return;
            func(services::internal::fromHandle(entity));
        }

        template<typename T, typename Func>
        T withEntityOr(services::EntityHandle entity, T defaultVal, Func&& func)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!services::internal::isValidHandle(entity, registry))
                return defaultVal;
            return func(services::internal::fromHandle(entity));
        }
    }

    AnimatorAdapter::AnimatorAdapter()
        : controller(std::make_unique<controllers::AnimatorSystemController>())
    {
    }

    AnimatorAdapter::~AnimatorAdapter() = default;

    void AnimatorAdapter::setFloat(services::EntityHandle entity, const std::string& paramName, float value)
    {
        withEntity(entity, [&](entt::entity e) { controller->setFloat(e, paramName, value); });
    }

    void AnimatorAdapter::setInt(services::EntityHandle entity, const std::string& paramName, int32_t value)
    {
        withEntity(entity, [&](entt::entity e) { controller->setInt(e, paramName, value); });
    }

    void AnimatorAdapter::setBool(services::EntityHandle entity, const std::string& paramName, bool value)
    {
        withEntity(entity, [&](entt::entity e) { controller->setBool(e, paramName, value); });
    }

    void AnimatorAdapter::setTrigger(services::EntityHandle entity, const std::string& paramName)
    {
        withEntity(entity, [&](entt::entity e) { controller->setTrigger(e, paramName); });
    }

    float AnimatorAdapter::getFloat(services::EntityHandle entity, const std::string& paramName) const
    {
        return withEntityOr<float>(entity, 0.0f, [&](entt::entity e) { return controller->getFloat(e, paramName); });
    }

    int32_t AnimatorAdapter::getInt(services::EntityHandle entity, const std::string& paramName) const
    {
        return withEntityOr<int32_t>(entity, 0, [&](entt::entity e) { return controller->getInt(e, paramName); });
    }

    bool AnimatorAdapter::getBool(services::EntityHandle entity, const std::string& paramName) const
    {
        return withEntityOr<bool>(entity, false, [&](entt::entity e) { return controller->getBool(e, paramName); });
    }

    void AnimatorAdapter::play(services::EntityHandle entity)
    {
        withEntity(entity, [&](entt::entity e) { controller->play(e); });
    }

    void AnimatorAdapter::pause(services::EntityHandle entity)
    {
        withEntity(entity, [&](entt::entity e) { controller->pause(e); });
    }

    void AnimatorAdapter::stop(services::EntityHandle entity)
    {
        withEntity(entity, [&](entt::entity e) { controller->stop(e); });
    }

    void AnimatorAdapter::reset(services::EntityHandle entity)
    {
        withEntity(entity, [&](entt::entity e) { controller->reset(e); });
    }

    bool AnimatorAdapter::isPlaying(services::EntityHandle entity) const
    {
        return withEntityOr<bool>(entity, false, [&](entt::entity e) { return controller->isPlaying(e); });
    }

    bool AnimatorAdapter::isBlending(services::EntityHandle entity) const
    {
        return withEntityOr<bool>(entity, false, [&](entt::entity e) { return controller->isBlending(e); });
    }

    std::string AnimatorAdapter::getCurrentState(services::EntityHandle entity) const
    {
        return withEntityOr<std::string>(entity, "", [&](entt::entity e) { return controller->getCurrentState(e); });
    }

    float AnimatorAdapter::getNormalizedTime(services::EntityHandle entity) const
    {
        return withEntityOr<float>(entity, 0.0f, [&](entt::entity e) { return controller->getNormalizedTime(e); });
    }

    bool AnimatorAdapter::hasAnimator(services::EntityHandle entity) const
    {
        return withEntityOr<bool>(entity, false, [&](entt::entity e) { return controller->hasAnimator(e); });
    }

    bool AnimatorAdapter::forceTransitionTo(services::EntityHandle entity, const std::string& stateName,
                                            float blendDuration)
    {
        return withEntityOr<bool>(entity, false, [&](entt::entity e) {
            return controller->forceTransitionTo(e, stateName, blendDuration);
        });
    }

    void AnimatorAdapter::setRootMotion(services::EntityHandle entity, bool enabled)
    {
        withEntity(entity, [&](entt::entity e) { controller->setRootMotion(e, enabled); });
    }

    bool AnimatorAdapter::getRootMotion(services::EntityHandle entity) const
    {
        return withEntityOr<bool>(entity, false, [&](entt::entity e) { return controller->getRootMotion(e); });
    }

    AnimatorAdapter::BudgetStats AnimatorAdapter::getBudgetStats() const
    {
        BudgetStats stats{};
        auto& animSys = animation::RuntimeAnimatorSystem::instance();
        stats.totalAnimators = animSys.getTotalAnimatorCount();
        stats.culledEntities = animSys.getCulledEntityCount();
        stats.pendingStreamingInits = animSys.getPendingInitCount();

        const auto& lodMgr = animSys.getLODManager();
        for (int i = 0; i < 4; ++i)
        {
            stats.lodCounts[i] = lodMgr.getLODCount(static_cast<animation::AnimationLODLevel>(i));
        }

        return stats;
    }
}
