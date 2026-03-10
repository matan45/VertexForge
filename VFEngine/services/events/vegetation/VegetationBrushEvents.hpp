#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../../utilities/vegetation/VegetationTypes.hpp"
#include <glm/glm.hpp>
#include <optional>

namespace events::vegetationBrush
{
    // ---- Commands ----

    struct ApplyVegetationDensityBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        float deltaTime = 0.0f;
        bool invert = false;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyVegetationDensityBrush"; }
    };

    struct SetDensityBrushParamsCommand : ICommand<>
    {
        ::vegetation::DensityBrushParams params;

        std::string_view getName() const override { return "SetDensityBrushParams"; }
    };

    struct SetDensityBrushTypeCommand : ICommand<>
    {
        ::vegetation::DensityBrushType type;

        std::string_view getName() const override { return "SetDensityBrushType"; }
    };

    // ---- Mode Commands ----

    struct SetVegetationBrushModeActiveCommand : ICommand<>
    {
        bool active;

        std::string_view getName() const override { return "SetVegetationBrushModeActive"; }
    };

    // ---- Queries ----

    struct GetDensityBrushParamsQuery : IQuery<::vegetation::DensityBrushParams>
    {
        std::string_view getName() const override { return "GetDensityBrushParams"; }
    };

    struct GetDensityBrushTypeQuery : IQuery<::vegetation::DensityBrushType>
    {
        std::string_view getName() const override { return "GetDensityBrushType"; }
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

    struct VegetationDensityBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        ::vegetation::DensityBrushType type;

        std::string_view getName() const override { return "VegetationDensityBrushApplied"; }
    };

    struct DensityBrushParamsChangedNotification : INotification
    {
        ::vegetation::DensityBrushParams params;

        std::string_view getName() const override { return "DensityBrushParamsChanged"; }
    };

    struct DensityBrushTypeChangedNotification : INotification
    {
        ::vegetation::DensityBrushType type;

        std::string_view getName() const override { return "DensityBrushTypeChanged"; }
    };

    struct VegetationBrushModeChangedNotification : INotification
    {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "VegetationBrushModeChanged"; }
    };
}
