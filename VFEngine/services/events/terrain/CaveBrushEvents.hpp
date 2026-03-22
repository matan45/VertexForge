#pragma once
#include "../EventTypes.hpp"
#include "../../utilities/terrain/CaveBrushTypes.hpp"
#include <glm/glm.hpp>

namespace events::caveBrush
{
    struct SetCaveBrushParamsCommand : ICommand<>
    {
        ::terrain::CaveBrushParams params;

        std::string_view getName() const override { return "SetCaveBrushParams"; }
    };

    struct SetCaveBrushRadiusCommand : ICommand<>
    {
        float radius;

        std::string_view getName() const override { return "SetCaveBrushRadius"; }
    };

    struct SetCaveBrushStrengthCommand : ICommand<>
    {
        float strength;

        std::string_view getName() const override { return "SetCaveBrushStrength"; }
    };

    struct SetCaveBrushFalloffCommand : ICommand<>
    {
        ::terrain::BrushFalloff falloff;

        std::string_view getName() const override { return "SetCaveBrushFalloff"; }
    };

    struct SetCaveBrushShapeCommand : ICommand<>
    {
        ::terrain::BrushShape shape;

        std::string_view getName() const override { return "SetCaveBrushShape"; }
    };

    struct SetCaveBrushTypeCommand : ICommand<>
    {
        ::terrain::CaveBrushType type;

        std::string_view getName() const override { return "SetCaveBrushType"; }
    };

    struct GetCaveBrushParamsQuery : IQuery<::terrain::CaveBrushParams>
    {
        std::string_view getName() const override { return "GetCaveBrushParams"; }
    };

    struct GetCaveBrushTypeQuery : IQuery<::terrain::CaveBrushType>
    {
        std::string_view getName() const override { return "GetCaveBrushType"; }
    };

    struct ApplyCaveBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        float deltaTime = 0.0f;
        bool invert = false;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyCaveBrush"; }
    };

    struct CaveBrushParamsChangedNotification : INotification
    {
        ::terrain::CaveBrushParams params;

        std::string_view getName() const override { return "CaveBrushParamsChanged"; }
    };

    struct CaveBrushTypeChangedNotification : INotification
    {
        ::terrain::CaveBrushType type;

        std::string_view getName() const override { return "CaveBrushTypeChanged"; }
    };

    struct CaveBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        ::terrain::CaveBrushType type;

        std::string_view getName() const override { return "CaveBrushApplied"; }
    };
}
