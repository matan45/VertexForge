#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../../utilities/vegetation/VegetationTypes.hpp"
#include <glm/glm.hpp>
#include <optional>

namespace events::vegetationBrush
{
    // ---- Commands ----

    struct ApplyVegetationBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
        float deltaTime = 0.0f;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyVegetationBrush"; }
    };

    struct SetVegetationBrushParamsCommand : ICommand<>
    {
        ::vegetation::VegetationBrushParams params;

        std::string_view getName() const override { return "SetVegetationBrushParams"; }
    };

    struct SetVegetationBrushTypeCommand : ICommand<>
    {
        ::vegetation::VegetationBrushType type;

        std::string_view getName() const override { return "SetVegetationBrushType"; }
    };

    // Ends the current paint/erase stroke and records a single undo entry for it.
    struct FinalizeVegetationBrushCommand : ICommand<>
    {
        std::string_view getName() const override { return "FinalizeVegetationBrush"; }
    };

    // ---- Mode Commands ----

    struct SetVegetationBrushModeActiveCommand : ICommand<>
    {
        bool active;

        std::string_view getName() const override { return "SetVegetationBrushModeActive"; }
    };

    // ---- Queries ----

    struct GetVegetationBrushParamsQuery : IQuery<::vegetation::VegetationBrushParams>
    {
        std::string_view getName() const override { return "GetVegetationBrushParams"; }
    };

    struct GetVegetationBrushTypeQuery : IQuery<::vegetation::VegetationBrushType>
    {
        std::string_view getName() const override { return "GetVegetationBrushType"; }
    };

    struct IsVegetationBrushModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsVegetationBrushModeActive"; }
    };

    struct GetVegetationBrushTargetEntityQuery : IQuery<std::optional<services::EntityHandle>>
    {
        std::string_view getName() const override { return "GetVegetationBrushTargetEntity"; }
    };

    // ---- Notifications ----

    struct VegetationBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        uint32_t placedCount = 0;
        uint32_t erasedCount = 0;

        std::string_view getName() const override { return "VegetationBrushApplied"; }
    };

    struct VegetationBrushModeChangedNotification : INotification
    {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "VegetationBrushModeChanged"; }
    };

    struct VegetationBrushParamsChangedNotification : INotification
    {
        ::vegetation::VegetationBrushParams params;

        std::string_view getName() const override { return "VegetationBrushParamsChanged"; }
    };
}
