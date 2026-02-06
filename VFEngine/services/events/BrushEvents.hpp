#pragma once
#include "EventTypes.hpp"
#include "../../utilities/terrain/BrushTypes.hpp"
#include <glm/glm.hpp>

namespace events::brush
{
    struct SetBrushParamsCommand : ICommand<>
    {
        ::terrain::BrushParams params;

        std::string_view getName() const override { return "SetBrushParams"; }
    };

    struct SetBrushRadiusCommand : ICommand<>
    {
        float radius;

        std::string_view getName() const override { return "SetBrushRadius"; }
    };

    struct SetBrushStrengthCommand : ICommand<>
    {
        float strength;

        std::string_view getName() const override { return "SetBrushStrength"; }
    };

    struct SetBrushFalloffCommand : ICommand<>
    {
        ::terrain::BrushFalloff falloff;

        std::string_view getName() const override { return "SetBrushFalloff"; }
    };

    struct SetBrushShapeCommand : ICommand<>
    {
        ::terrain::BrushShape shape;

        std::string_view getName() const override { return "SetBrushShape"; }
    };

    struct SetBrushTypeCommand : ICommand<>
    {
        ::terrain::BrushType type;

        std::string_view getName() const override { return "SetBrushType"; }
    };

    struct ApplyBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        float deltaTime = 0.0f;
        bool invert = false;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyBrush"; }
    };

    struct GetBrushParamsQuery : IQuery<::terrain::BrushParams>
    {
        std::string_view getName() const override { return "GetBrushParams"; }
    };

    struct GetBrushTypeQuery : IQuery<::terrain::BrushType>
    {
        std::string_view getName() const override { return "GetBrushType"; }
    };

    struct BrushParamsChangedNotification : INotification
    {
        ::terrain::BrushParams params;

        std::string_view getName() const override { return "BrushParamsChanged"; }
    };

    struct BrushTypeChangedNotification : INotification
    {
        ::terrain::BrushType type;

        std::string_view getName() const override { return "BrushTypeChanged"; }
    };

    struct BrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        ::terrain::BrushType type;

        std::string_view getName() const override { return "BrushApplied"; }
    };
}
