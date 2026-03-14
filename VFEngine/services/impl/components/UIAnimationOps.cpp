#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"

namespace services {

    // ========== Conversion Helpers ==========

    namespace {

        UIAnimationClipData toClipData(const components::UIAnimationClip& clip) {
            UIAnimationClipData data;
            data.property = static_cast<uint8_t>(clip.property);
            data.startValue = clip.startValue;
            data.endValue = clip.endValue;
            data.duration = clip.duration;
            data.delay = clip.delay;
            data.easing = static_cast<uint8_t>(clip.easing);
            data.loopMode = static_cast<uint8_t>(clip.loopMode);
            return data;
        }

        components::UIAnimationClip fromClipData(const UIAnimationClipData& data) {
            components::UIAnimationClip clip;
            clip.property = static_cast<components::UITweenProperty>(data.property);
            clip.startValue = data.startValue;
            clip.endValue = data.endValue;
            clip.duration = data.duration;
            clip.delay = data.delay;
            clip.easing = static_cast<components::UIEasingFunction>(data.easing);
            clip.loopMode = static_cast<components::UIAnimationLoopMode>(data.loopMode);
            return clip;
        }

        UIAnimationNodeData toNodeData(const components::UIAnimationNode& node) {
            UIAnimationNodeData data;
            data.type = static_cast<uint8_t>(node.type);
            data.clip = toClipData(node.clip);
            data.loopMode = static_cast<uint8_t>(node.loopMode);

            data.children.reserve(node.children.size());
            for (const auto& child : node.children) {
                data.children.push_back(toNodeData(child));
            }

            return data;
        }

        components::UIAnimationNode fromNodeData(const UIAnimationNodeData& data) {
            components::UIAnimationNode node;
            node.type = static_cast<components::UIAnimationNodeType>(data.type);
            node.clip = fromClipData(data.clip);
            node.loopMode = static_cast<components::UIAnimationLoopMode>(data.loopMode);

            node.children.reserve(data.children.size());
            for (const auto& child : data.children) {
                node.children.push_back(fromNodeData(child));
            }

            return node;
        }

    } // anonymous namespace

    // ========== UI Animation Operations ==========

    bool UIComponentService::addUIAnimationComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIAnimationComponent>();

        // Auto-add UIRectComponent if missing (animation needs rect for transform properties)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUIAnimationComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIAnimationComponent>();
        return true;
    }

    bool UIComponentService::hasUIAnimationComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIAnimationComponent>();
    }

    std::optional<UIAnimationData> UIComponentService::getUIAnimationData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIAnimationComponent>();

        UIAnimationData data;
        data.rootNode = toNodeData(comp.rootNode);
        data.autoPlay = comp.autoPlay;
        return data;
    }

    bool UIComponentService::setUIAnimationData(EntityHandle entity, const UIAnimationData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIAnimationComponent>();
        comp.rootNode = fromNodeData(data.rootNode);
        comp.autoPlay = data.autoPlay;
        return true;
    }

    bool UIComponentService::playUIAnimation(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIAnimationComponent>();
        comp.isPlaying = true;
        comp.isPaused = false;
        comp.elapsedTime = 0.0f;
        comp.startedFired = false;
        comp.completedFired = false;
        return true;
    }

    bool UIComponentService::stopUIAnimation(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIAnimationComponent>();
        comp.isPlaying = false;
        comp.isPaused = false;
        return true;
    }

    bool UIComponentService::pauseUIAnimation(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIAnimationComponent>();
        if (!comp.isPlaying) {
            return false;
        }

        comp.isPaused = true;
        return true;
    }

    bool UIComponentService::resumeUIAnimation(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIAnimationComponent>();
        if (!comp.isPlaying) {
            return false;
        }

        comp.isPaused = false;
        return true;
    }

    bool UIComponentService::isUIAnimationPlaying(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIAnimationComponent>()) {
            return false;
        }

        const auto& comp = sceneEntity.getComponent<components::UIAnimationComponent>();
        return comp.isPlaying;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerAnimationHandlers(events::EventDispatcher& dispatcher) {
        // Animation commands
        dispatcher.registerCommandHandler<events::ui::AddUIAnimationComponentCommand>(
            [this](const events::ui::AddUIAnimationComponentCommand& cmd) {
                return addUIAnimationComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIAnimationComponentCommand>(
            [this](const events::ui::RemoveUIAnimationComponentCommand& cmd) {
                return removeUIAnimationComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIAnimationDataCommand>(
            [this](const events::ui::SetUIAnimationDataCommand& cmd) {
                return setUIAnimationData(cmd.entity, cmd.animationData);
            });

        dispatcher.registerCommandHandler<events::ui::PlayUIAnimationCommand>(
            [this](const events::ui::PlayUIAnimationCommand& cmd) {
                return playUIAnimation(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::StopUIAnimationCommand>(
            [this](const events::ui::StopUIAnimationCommand& cmd) {
                return stopUIAnimation(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::PauseUIAnimationCommand>(
            [this](const events::ui::PauseUIAnimationCommand& cmd) {
                return pauseUIAnimation(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::ResumeUIAnimationCommand>(
            [this](const events::ui::ResumeUIAnimationCommand& cmd) {
                return resumeUIAnimation(cmd.entity);
            });

        // Animation queries
        dispatcher.registerQueryHandler<events::ui::HasUIAnimationComponentQuery>(
            [this](const events::ui::HasUIAnimationComponentQuery& query) {
                return hasUIAnimationComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIAnimationDataQuery>(
            [this](const events::ui::GetUIAnimationDataQuery& query) {
                return getUIAnimationData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::IsUIAnimationPlayingQuery>(
            [this](const events::ui::IsUIAnimationPlayingQuery& query) {
                return isUIAnimationPlaying(query.entity);
            });
    }

}
