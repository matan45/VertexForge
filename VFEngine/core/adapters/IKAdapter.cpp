#include "IKAdapter.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "print/Logger.hpp"

namespace core
{
    IKAdapter::IKAdapter() = default;
    IKAdapter::~IKAdapter() = default;

    std::optional<entt::entity> IKAdapter::resolveEntity(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(handle, registry))
            return std::nullopt;
        return services::internal::fromHandle(handle);
    }

    bool IKAdapter::addIKComponent(services::EntityHandle entity)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.all_of<components::IKTargetComponent>(*resolved))
            return false;

        auto& ikComp = registry.emplace<components::IKTargetComponent>(*resolved);

        // Auto-load IK chains from the entity's mesh file if available
        if (registry.all_of<components::MeshComponent>(*resolved))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(*resolved);
            if (!meshComp.meshPath.empty())
            {
                auto stream = resource::MeshStreamResource::openStream(meshComp.meshPath);
                if (stream && stream->hasSkeletonData())
                {
                    resource::SkeletonData skeleton;
                    if (stream->readSkeleton(skeleton) && !skeleton.ikChains.empty())
                    {
                        ikComp.chains = skeleton.ikChains;
                        loggerInfo("[IKAdapter] Auto-loaded {} IK chains from mesh: {}",
                                   ikComp.chains.size(), meshComp.meshPath);
                    }
                }
            }
        }

        return true;
    }

    bool IKAdapter::removeIKComponent(services::EntityHandle entity)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return false;

        registry.remove<components::IKTargetComponent>(*resolved);
        return true;
    }

    bool IKAdapter::hasIKComponent(services::EntityHandle entity) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        return registry.all_of<components::IKTargetComponent>(*resolved);
    }

    bool IKAdapter::addChain(services::EntityHandle entity, const std::string& chainName,
                              const std::string& tipBoneName,
                              const std::vector<std::string>& chainBoneNames)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return false;

        auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);

        for (const auto& chain : ikComp.chains)
        {
            if (chain.chainName == chainName)
                return false;
        }

        animator::ik::IKChainConfig config;
        config.chainName = chainName;
        config.tipBoneName = tipBoneName;
        config.chainBoneNames = chainBoneNames;
        ikComp.chains.push_back(std::move(config));

        ikComp.isInitialized = false;
        ikComp.runtimeStates.clear();

        return true;
    }

    bool IKAdapter::removeChain(services::EntityHandle entity, const std::string& chainName)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return false;

        auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);

        auto it = std::find_if(ikComp.chains.begin(), ikComp.chains.end(),
                               [&](const auto& c) { return c.chainName == chainName; });
        if (it == ikComp.chains.end())
            return false;

        size_t idx = std::distance(ikComp.chains.begin(), it);
        ikComp.chains.erase(it);

        if (idx < ikComp.runtimeStates.size())
            ikComp.runtimeStates.erase(ikComp.runtimeStates.begin() + idx);

        return true;
    }

    void IKAdapter::updateChainConfig(services::EntityHandle entity, const std::string& chainName,
                                       const std::string& tipBoneName,
                                       const std::vector<std::string>& chainBoneNames,
                                       const std::vector<animator::ik::JointConstraint>& constraints,
                                       float weight, bool enabled)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return;

        auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);

        for (size_t i = 0; i < ikComp.chains.size(); ++i)
        {
            if (ikComp.chains[i].chainName == chainName)
            {
                ikComp.chains[i].tipBoneName = tipBoneName;
                ikComp.chains[i].chainBoneNames = chainBoneNames;
                ikComp.chains[i].constraints = constraints;
                ikComp.chains[i].weight = weight;
                ikComp.chains[i].enabled = enabled;

                        if (i < ikComp.runtimeStates.size())
                {
                    ikComp.runtimeStates[i].resolvedTipIndex = -1;
                    ikComp.runtimeStates[i].resolvedBoneIndices.clear();
                }
                return;
            }
        }
    }

    void IKAdapter::setTarget(services::EntityHandle entity, const std::string& chainName,
                               const glm::vec3& position,
                               const std::optional<glm::quat>& rotation)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return;

        auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);

        if (ikComp.runtimeStates.size() != ikComp.chains.size())
            ikComp.runtimeStates.resize(ikComp.chains.size());

        for (size_t i = 0; i < ikComp.chains.size(); ++i)
        {
            if (ikComp.chains[i].chainName == chainName)
            {
                ikComp.runtimeStates[i].targetPosition = position;
                ikComp.runtimeStates[i].targetRotation = rotation;
                ikComp.runtimeStates[i].isActive = true;
                ikComp.runtimeStates[i].currentWeight = ikComp.chains[i].weight;
                return;
            }
        }

        loggerWarning("[IKAdapter] Chain '{}' not found on entity", chainName);
    }

    void IKAdapter::setChainWeight(services::EntityHandle entity, const std::string& chainName,
                                    float weight)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return;

        auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);

        for (auto& chain : ikComp.chains)
        {
            if (chain.chainName == chainName)
            {
                chain.weight = glm::clamp(weight, 0.0f, 1.0f);
                return;
            }
        }
    }

    void IKAdapter::setChainEnabled(services::EntityHandle entity, const std::string& chainName,
                                     bool enabled)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return;

        auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);

        for (auto& chain : ikComp.chains)
        {
            if (chain.chainName == chainName)
            {
                chain.enabled = enabled;
                return;
            }
        }
    }

    std::vector<std::string> IKAdapter::getChainNames(services::EntityHandle entity) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return {};

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return {};

        const auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);
        std::vector<std::string> names;
        names.reserve(ikComp.chains.size());
        for (const auto& chain : ikComp.chains)
            names.push_back(chain.chainName);

        return names;
    }

    float IKAdapter::getChainWeight(services::EntityHandle entity,
                                     const std::string& chainName) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return 0.0f;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return 0.0f;

        const auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);
        for (const auto& chain : ikComp.chains)
        {
            if (chain.chainName == chainName)
                return chain.weight;
        }
        return 0.0f;
    }

    bool IKAdapter::isChainEnabled(services::EntityHandle entity,
                                    const std::string& chainName) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::IKTargetComponent>(*resolved))
            return false;

        const auto& ikComp = registry.get<components::IKTargetComponent>(*resolved);
        for (const auto& chain : ikComp.chains)
        {
            if (chain.chainName == chainName)
                return chain.enabled;
        }
        return false;
    }
}
