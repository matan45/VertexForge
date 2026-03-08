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

    struct ApplyVegetationPlacementBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        float deltaTime = 0.0f;

        std::string_view getName() const override { return "ApplyVegetationPlacementBrush"; }
    };

    struct SetDensityBrushParamsCommand : ICommand<>
    {
        ::vegetation::DensityBrushParams params;

        std::string_view getName() const override { return "SetDensityBrushParams"; }
    };

    struct SetPlacementBrushParamsCommand : ICommand<>
    {
        ::vegetation::PlacementBrushParams params;

        std::string_view getName() const override { return "SetPlacementBrushParams"; }
    };

    struct SetDensityBrushTypeCommand : ICommand<>
    {
        ::vegetation::DensityBrushType type;

        std::string_view getName() const override { return "SetDensityBrushType"; }
    };

    struct SetPlacementBrushTypeCommand : ICommand<>
    {
        ::vegetation::PlacementBrushType type;

        std::string_view getName() const override { return "SetPlacementBrushType"; }
    };

    // ---- Mode Commands ----

    struct SetVegetationBrushModeActiveCommand : ICommand<>
    {
        bool active;

        std::string_view getName() const override { return "SetVegetationBrushModeActive"; }
    };

    struct SetVegetationPlacementModeActiveCommand : ICommand<>
    {
        bool active;

        std::string_view getName() const override { return "SetVegetationPlacementModeActive"; }
    };

    // ---- Queries ----

    struct GetDensityBrushParamsQuery : IQuery<::vegetation::DensityBrushParams>
    {
        std::string_view getName() const override { return "GetDensityBrushParams"; }
    };

    struct GetPlacementBrushParamsQuery : IQuery<::vegetation::PlacementBrushParams>
    {
        std::string_view getName() const override { return "GetPlacementBrushParams"; }
    };

    struct GetDensityBrushTypeQuery : IQuery<::vegetation::DensityBrushType>
    {
        std::string_view getName() const override { return "GetDensityBrushType"; }
    };

    struct GetPlacementBrushTypeQuery : IQuery<::vegetation::PlacementBrushType>
    {
        std::string_view getName() const override { return "GetPlacementBrushType"; }
    };

    struct IsVegetationBrushModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsVegetationBrushModeActive"; }
    };

    struct IsVegetationPlacementModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsVegetationPlacementModeActive"; }
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

    struct VegetationPlacementBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        ::vegetation::PlacementBrushType type;

        std::string_view getName() const override { return "VegetationPlacementBrushApplied"; }
    };

    struct DensityBrushParamsChangedNotification : INotification
    {
        ::vegetation::DensityBrushParams params;

        std::string_view getName() const override { return "DensityBrushParamsChanged"; }
    };

    struct PlacementBrushParamsChangedNotification : INotification
    {
        ::vegetation::PlacementBrushParams params;

        std::string_view getName() const override { return "PlacementBrushParamsChanged"; }
    };

    struct DensityBrushTypeChangedNotification : INotification
    {
        ::vegetation::DensityBrushType type;

        std::string_view getName() const override { return "DensityBrushTypeChanged"; }
    };

    struct PlacementBrushTypeChangedNotification : INotification
    {
        ::vegetation::PlacementBrushType type;

        std::string_view getName() const override { return "PlacementBrushTypeChanged"; }
    };

    struct VegetationBrushModeChangedNotification : INotification
    {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "VegetationBrushModeChanged"; }
    };

    struct VegetationPlacementModeChangedNotification : INotification
    {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "VegetationPlacementModeChanged"; }
    };
}
