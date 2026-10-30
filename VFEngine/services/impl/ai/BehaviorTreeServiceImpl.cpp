#include "BehaviorTreeServiceImpl.hpp"
#include "BehaviorTreeInitialValues.hpp"
#include "../../events/ai/BehaviorTreeEvents.hpp"
#include "../../events/ai/BehaviorTreeComponentEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace services
{
    namespace
    {
        // The asset reference of an existing .vfBehaviorTree (a project-relative path resolves against the
        // project root); invalid for any other path. Checked first because AssetRef::fromPath registers
        // (and writes a .vfmeta for) any path with a known asset extension, even a missing file.
        asset::AssetRef behaviorTreeAssetRef(const std::string& path)
        {
            std::string extension = std::filesystem::path(path).extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".vfbehaviortree")
                return asset::AssetRef::invalid();

            std::error_code ec;
            const std::string resolved = asset::AssetDatabase::instance().resolveAssetPath(path);
            if (!std::filesystem::is_regular_file(std::filesystem::path(resolved), ec))
                return asset::AssetRef::invalid();
            return asset::AssetRef::fromPath(path);
        }
    }

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

        dispatcher.registerCommandHandler<events::ai::ReloadBehaviorTreeAssetCommand>(
            [this](const auto& cmd)
            {
                provider->reloadAsset(cmd.treePath);
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

        dispatcher.registerQueryHandler<events::ai::GetBehaviorTreeStatusQuery>(
            [this](const auto& query)
            {
                return provider->getStatus(query.entity);
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

        // === Debug ===
        dispatcher.registerCommandHandler<events::ai::SetTreeDebugTargetCommand>(
            [this](const auto& cmd)
            {
                provider->setDebugTarget(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::ai::GetTreeRuntimeSnapshotQuery>(
            [this](const auto& query)
            {
                return provider->getRuntimeSnapshot(query.entity);
            });

        // VK-1660: on demand, without touching the editor debugger's debug target.
        dispatcher.registerQueryHandler<::events::ai::GetBehaviorTreeRuntimeStateQuery>(
            [this](const ::events::ai::GetBehaviorTreeRuntimeStateQuery& query)
            {
                return provider ? provider->getRuntimeState(query.entity) : behaviortree::BTRuntimeState{};
            });

        // === Dynamic subtree (VK-1457) ===
        dispatcher.registerCommandHandler<events::ai::SetDynamicSubtreeCommand>(
            [this](const auto& cmd)
            {
                provider->setDynamicSubtree(cmd.entity, cmd.tag, cmd.treePath);
            });

        // === Debug controls (VK-1457) ===
        dispatcher.registerCommandHandler<events::ai::SetTreeDebugPausedCommand>(
            [this](const auto& cmd)
            {
                provider->setDebugPaused(cmd.paused);
            });

        dispatcher.registerCommandHandler<events::ai::StepTreeDebugCommand>(
            [this](const auto&)
            {
                provider->stepDebug();
            });

        dispatcher.registerCommandHandler<events::ai::SetTreeBreakpointsCommand>(
            [this](const auto& cmd)
            {
                provider->setBreakpoints(cmd.entity, cmd.nodeIds);
            });

        dispatcher.registerQueryHandler<events::ai::GetAttachedBehaviorTreesQuery>(
            [this](const auto&)
            {
                std::vector<events::ai::BTAttachedTreeInfo> result;
                auto& registry = scene::EntityRegistry::getRegistry();

                // Enumerate the live runtimes, not the BehaviorTreeComponent view: a tree
                // attached at runtime via script (Blackboard::attachTree) has a runtime but
                // no component, so a component walk would miss it and the debugger would
                // wrongly report "no entities running this tree".
                for (auto handle : provider->getAttachedEntities())
                {
                    events::ai::BTAttachedTreeInfo info;
                    info.entity = handle;
                    info.treePath = provider->getTreePath(handle);

                    auto entity = internal::fromHandle(handle);
                    if (registry.valid(entity) && registry.all_of<components::NameComponent>(entity))
                    {
                        info.name = registry.get<components::NameComponent>(entity).name;
                    }
                    result.push_back(std::move(info));
                }
                return result;
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

        // === ECS Component data (VK-1660) ===
        dispatcher.registerQueryHandler<::events::ai::GetBehaviorTreeComponentDataQuery>(
            [](const ::events::ai::GetBehaviorTreeComponentDataQuery& query) -> std::optional<BehaviorTreeComponentData>
            {
                const auto& registry = scene::EntityRegistry::getRegistry();
                if (!query.entity.isValid()) return std::nullopt;
                const auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity)) return std::nullopt;
                const auto* bt = registry.try_get<components::BehaviorTreeComponent>(entity);
                if (!bt) return std::nullopt;
                return makeBehaviorTreeComponentData(registry, *bt);
            });

        dispatcher.registerCommandHandler<::events::ai::SetBehaviorTreeComponentDataCommand>(
            [](const ::events::ai::SetBehaviorTreeComponentDataCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!internal::isValidHandle(cmd.entity, registry)) return false;
                auto* bt = registry.try_get<components::BehaviorTreeComponent>(internal::fromHandle(cmd.entity));
                if (!bt) return false;

                // Refuse (changing nothing) a path that is not an existing behavior tree rather than
                // silently storing no tree.
                asset::AssetRef treeRef = asset::AssetRef::invalid();
                if (!cmd.data.treePath.empty())
                {
                    treeRef = behaviorTreeAssetRef(cmd.data.treePath);
                    if (!treeRef.isValid())
                    {
                        vfLogWarning("[BT] '{}' is not an existing behavior tree; the component was not changed",
                                     cmd.data.treePath);
                        return false;
                    }
                }

                std::vector<behaviortree::BlackboardInitialValue> initialValues;
                initialValues.reserve(cmd.data.initialValues.size());
                for (const auto& value : cmd.data.initialValues)
                    initialValues.push_back(makeStoredInitialValue(registry, value));

                bt->behaviorTreeRef = std::move(treeRef);
                bt->enabled = cmd.data.enabled;
                bt->initialValues = std::move(initialValues);
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
