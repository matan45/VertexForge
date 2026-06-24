#pragma once
#include "../../services/providers/physics/IIKProvider.hpp"
#include <entt/entt.hpp>
#include <optional>

namespace core
{
    class IKAdapter : public services::IIKProvider
    {
    public:
        IKAdapter();
        ~IKAdapter() override;

        bool addIKComponent(services::EntityHandle entity) override;
        bool removeIKComponent(services::EntityHandle entity) override;
        [[nodiscard]] bool hasIKComponent(services::EntityHandle entity) const override;

        bool addChain(services::EntityHandle entity, const std::string& chainName,
                      const std::string& tipBoneName,
                      const std::vector<std::string>& chainBoneNames) override;
        bool removeChain(services::EntityHandle entity, const std::string& chainName) override;
        void updateChainConfig(services::EntityHandle entity, const std::string& chainName,
                               const std::string& tipBoneName,
                               const std::vector<std::string>& chainBoneNames,
                               const std::vector<animator::ik::JointConstraint>& constraints,
                               float weight, bool enabled) override;

        void setTarget(services::EntityHandle entity, const std::string& chainName,
                       const glm::vec3& position,
                       const std::optional<glm::quat>& rotation) override;
        void setChainWeight(services::EntityHandle entity, const std::string& chainName,
                            float weight) override;
        void setChainEnabled(services::EntityHandle entity, const std::string& chainName,
                             bool enabled) override;

        [[nodiscard]] std::vector<std::string> getChainNames(services::EntityHandle entity) const override;
        [[nodiscard]] std::vector<animator::ik::IKChainConfig> getChainConfigs(
            services::EntityHandle entity) const override;
        [[nodiscard]] float getChainWeight(services::EntityHandle entity,
                                            const std::string& chainName) const override;
        [[nodiscard]] bool isChainEnabled(services::EntityHandle entity,
                                           const std::string& chainName) const override;

    private:
        static std::optional<entt::entity> resolveEntity(services::EntityHandle handle);
    };
}
