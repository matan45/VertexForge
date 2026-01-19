#include "AnimatorAdapter.hpp"
#include "../../graphics/controllers/AnimatorSystemController.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"

namespace core
{
    AnimatorAdapter::AnimatorAdapter()
        : controller(std::make_unique<controllers::AnimatorSystemController>())
    {
    }

    AnimatorAdapter::~AnimatorAdapter() = default;

    void AnimatorAdapter::setFloat(services::EntityHandle entity, const std::string& paramName, float value)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->setFloat(services::internal::fromHandle(entity), paramName, value);
    }

    void AnimatorAdapter::setInt(services::EntityHandle entity, const std::string& paramName, int32_t value)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->setInt(services::internal::fromHandle(entity), paramName, value);
    }

    void AnimatorAdapter::setBool(services::EntityHandle entity, const std::string& paramName, bool value)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->setBool(services::internal::fromHandle(entity), paramName, value);
    }

    void AnimatorAdapter::setTrigger(services::EntityHandle entity, const std::string& paramName)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->setTrigger(services::internal::fromHandle(entity), paramName);
    }

    float AnimatorAdapter::getFloat(services::EntityHandle entity, const std::string& paramName) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return 0.0f;

        return controller->getFloat(services::internal::fromHandle(entity), paramName);
    }

    int32_t AnimatorAdapter::getInt(services::EntityHandle entity, const std::string& paramName) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return 0;

        return controller->getInt(services::internal::fromHandle(entity), paramName);
    }

    bool AnimatorAdapter::getBool(services::EntityHandle entity, const std::string& paramName) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return false;

        return controller->getBool(services::internal::fromHandle(entity), paramName);
    }

    void AnimatorAdapter::play(services::EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->play(services::internal::fromHandle(entity));
    }

    void AnimatorAdapter::pause(services::EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->pause(services::internal::fromHandle(entity));
    }

    void AnimatorAdapter::stop(services::EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->stop(services::internal::fromHandle(entity));
    }

    void AnimatorAdapter::reset(services::EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return;

        controller->reset(services::internal::fromHandle(entity));
    }

    bool AnimatorAdapter::isPlaying(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return false;

        return controller->isPlaying(services::internal::fromHandle(entity));
    }

    bool AnimatorAdapter::isBlending(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return false;

        return controller->isBlending(services::internal::fromHandle(entity));
    }

    std::string AnimatorAdapter::getCurrentState(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return "";

        return controller->getCurrentState(services::internal::fromHandle(entity));
    }

    float AnimatorAdapter::getNormalizedTime(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return 0.0f;

        return controller->getNormalizedTime(services::internal::fromHandle(entity));
    }

    bool AnimatorAdapter::hasAnimator(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return false;

        return controller->hasAnimator(services::internal::fromHandle(entity));
    }

    bool AnimatorAdapter::forceTransitionTo(services::EntityHandle entity, const std::string& stateName,
                                            float blendDuration)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(entity, registry))
            return false;

        return controller->forceTransitionTo(services::internal::fromHandle(entity), stateName, blendDuration);
    }
}
