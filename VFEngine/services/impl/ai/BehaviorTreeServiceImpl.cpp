#include "BehaviorTreeServiceImpl.hpp"
#include "../../events/ai/BehaviorTreeEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <cassert>

namespace services
{
    BehaviorTreeServiceImpl::BehaviorTreeServiceImpl(IBehaviorTreeProvider* provider)
        : provider(provider)
    {
        assert(provider && "IBehaviorTreeProvider must not be null");
    }

    BehaviorTreeServiceImpl::~BehaviorTreeServiceImpl() = default;

    void BehaviorTreeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // === Commands ===
        dispatcher.registerCommandHandler<events::ai::AttachBehaviorTreeCommand>(
            [this](const auto& cmd)
            {
                return attachTree(cmd.entity, cmd.treePath);
            });

        dispatcher.registerCommandHandler<events::ai::DetachBehaviorTreeCommand>(
            [this](const auto& cmd)
            {
                detachTree(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ai::SetBehaviorTreeEnabledCommand>(
            [this](const auto& cmd)
            {
                setEnabled(cmd.entity, cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::ai::SetBlackboardValueCommand>(
            [this](const auto& cmd)
            {
                provider->setBlackboardValue(cmd.entity, cmd.key, cmd.value);
            });

        // === Queries ===
        dispatcher.registerQueryHandler<events::ai::HasBehaviorTreeQuery>(
            [this](const auto& query)
            {
                return provider->hasTree(query.entity);
            });

        dispatcher.registerQueryHandler<events::ai::GetBehaviorTreePathQuery>(
            [this](const auto& query)
            {
                return provider->getTreePath(query.entity);
            });

        dispatcher.registerQueryHandler<events::ai::IsBehaviorTreeEnabledQuery>(
            [this](const auto& query)
            {
                return provider->isEnabled(query.entity);
            });

        dispatcher.registerQueryHandler<events::ai::GetBlackboardValueQuery>(
            [this](const auto& query)
            {
                return provider->getBlackboardValue(query.entity, query.key);
            });

        dispatcher.registerQueryHandler<events::ai::HasBlackboardKeyQuery>(
            [this](const auto& query)
            {
                return provider->hasBlackboardKey(query.entity, query.key);
            });

        // === ECS Component add/remove ===
        dispatcher.registerCommandHandler<::events::scene::AddBehaviorTreeComponentCommand>(
            [](const auto& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity)) return false;
                if (registry.all_of<components::BehaviorTreeComponent>(entity)) return false;
                registry.emplace<components::BehaviorTreeComponent>(entity);
                return true;
            });

        dispatcher.registerCommandHandler<::events::scene::RemoveBehaviorTreeComponentCommand>(
            [this](const auto& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity)) return false;
                if (!registry.all_of<components::BehaviorTreeComponent>(entity)) return false;
                detachTree(cmd.entity);
                registry.remove<components::BehaviorTreeComponent>(entity);
                return true;
            });
    }

    bool BehaviorTreeServiceImpl::attachTree(EntityHandle entity, const std::string& treePath)
    {
        return provider->attachTree(entity, treePath);
    }

    void BehaviorTreeServiceImpl::detachTree(EntityHandle entity)
    {
        provider->detachTree(entity);
    }

    void BehaviorTreeServiceImpl::setEnabled(EntityHandle entity, bool enabled)
    {
        provider->setEnabled(entity, enabled);
    }

    void BehaviorTreeServiceImpl::updateAll(float deltaTime)
    {
        provider->updateAll(deltaTime);
    }

    void BehaviorTreeServiceImpl::stopAll()
    {
        provider->stopAll();
    }
}
