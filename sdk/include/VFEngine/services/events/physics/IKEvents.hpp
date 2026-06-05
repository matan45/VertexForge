#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "animator/IKTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <optional>

namespace events::ik
{
    // === Commands ===

    struct SetIKTargetCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        glm::vec3 targetPosition{0.0f};
        std::optional<glm::quat> targetRotation;
        std::string_view getName() const override { return "SetIKTarget"; }
    };

    struct SetIKChainWeightCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        float weight = 1.0f;
        std::string_view getName() const override { return "SetIKChainWeight"; }
    };

    struct SetIKChainEnabledCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        bool enabled = true;
        std::string_view getName() const override { return "SetIKChainEnabled"; }
    };

    struct AddIKComponentCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "AddIKComponent"; }
    };

    struct RemoveIKComponentCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveIKComponent"; }
    };

    struct AddIKChainCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        std::string tipBoneName;
        std::vector<std::string> chainBoneNames;
        std::string_view getName() const override { return "AddIKChain"; }
    };

    struct RemoveIKChainCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        std::string_view getName() const override { return "RemoveIKChain"; }
    };

    struct UpdateIKChainConfigCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        std::string tipBoneName;
        std::vector<std::string> chainBoneNames;
        std::vector<animator::ik::JointConstraint> constraints;
        float weight = 1.0f;
        bool enabled = true;
        std::string_view getName() const override { return "UpdateIKChainConfig"; }
    };

    // === Queries ===

    struct GetIKChainNamesQuery : ::events::IQuery<std::vector<std::string>>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "GetIKChainNames"; }
    };

    struct GetIKChainWeightQuery : ::events::IQuery<float>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        std::string_view getName() const override { return "GetIKChainWeight"; }
    };

    struct IsIKChainEnabledQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string chainName;
        std::string_view getName() const override { return "IsIKChainEnabled"; }
    };

    struct HasIKComponentQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "HasIKComponent"; }
    };

    // === Notifications ===

    struct IKChainDataSavedNotification : ::events::INotification
    {
        std::string meshPath;
        std::string_view getName() const override { return "IKChainDataSaved"; }
    };
}
