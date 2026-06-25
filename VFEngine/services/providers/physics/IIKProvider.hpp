#pragma once

#include "../../data/EntityHandle.hpp"
#include "animator/IKTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <optional>

namespace services
{
    class IIKProvider
    {
    public:
        virtual ~IIKProvider() = default;

        // Component management
        virtual bool addIKComponent(EntityHandle entity) = 0;
        virtual bool removeIKComponent(EntityHandle entity) = 0;
        [[nodiscard]] virtual bool hasIKComponent(EntityHandle entity) const = 0;

        // Chain management
        virtual bool addChain(EntityHandle entity, const std::string& chainName,
                              const std::string& tipBoneName,
                              const std::vector<std::string>& chainBoneNames) = 0;
        virtual bool removeChain(EntityHandle entity, const std::string& chainName) = 0;
        virtual void updateChainConfig(EntityHandle entity, const std::string& chainName,
                                       const std::string& tipBoneName,
                                       const std::vector<std::string>& chainBoneNames,
                                       const std::vector<animator::ik::JointConstraint>& constraints,
                                       float weight, bool enabled) = 0;

        // Runtime target control
        virtual void setTarget(EntityHandle entity, const std::string& chainName,
                               const glm::vec3& position,
                               const std::optional<glm::quat>& rotation) = 0;
        virtual void setChainWeight(EntityHandle entity, const std::string& chainName,
                                    float weight) = 0;
        virtual void setChainEnabled(EntityHandle entity, const std::string& chainName,
                                     bool enabled) = 0;

        // Queries
        [[nodiscard]] virtual std::vector<std::string> getChainNames(EntityHandle entity) const = 0;
        // VK-1433 Phase 4 — full chain configs (the Prefab Rig live-desc builder needs the whole
        // IKChainConfig, not just names, to reconstruct PrefabRigDescDTO.ik).
        [[nodiscard]] virtual std::vector<animator::ik::IKChainConfig> getChainConfigs(
            EntityHandle entity) const = 0;
        [[nodiscard]] virtual float getChainWeight(EntityHandle entity,
                                                    const std::string& chainName) const = 0;
        [[nodiscard]] virtual bool isChainEnabled(EntityHandle entity,
                                                   const std::string& chainName) const = 0;
    };
}
