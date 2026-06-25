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
    class IIKProvider;

    class IKComponentService
    {
    private:
        IIKProvider* ikProvider;

    public:
        explicit IKComponentService(IIKProvider* provider);

        bool addIKComponent(EntityHandle entity);
        bool removeIKComponent(EntityHandle entity);
        bool hasIKComponent(EntityHandle entity) const;

        bool addChain(EntityHandle entity, const std::string& chainName,
                      const std::string& tipBoneName,
                      const std::vector<std::string>& chainBoneNames);
        bool removeChain(EntityHandle entity, const std::string& chainName);
        void updateChainConfig(EntityHandle entity, const std::string& chainName,
                               const std::string& tipBoneName,
                               const std::vector<std::string>& chainBoneNames,
                               const std::vector<animator::ik::JointConstraint>& constraints,
                               float weight, bool enabled);

        void setTarget(EntityHandle entity, const std::string& chainName,
                       const glm::vec3& position,
                       const std::optional<glm::quat>& rotation);
        void setChainWeight(EntityHandle entity, const std::string& chainName, float weight);
        void setChainEnabled(EntityHandle entity, const std::string& chainName, bool enabled);

        std::vector<std::string> getChainNames(EntityHandle entity) const;
        std::vector<animator::ik::IKChainConfig> getChainConfigs(EntityHandle entity) const;
        float getChainWeight(EntityHandle entity, const std::string& chainName) const;
        bool isChainEnabled(EntityHandle entity, const std::string& chainName) const;

        void registerEventHandlers();
    };
}
