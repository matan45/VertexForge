#pragma once
#include "../EventTypes.hpp"
#include "../../utilities/terrain/HoleBrushTypes.hpp"
#include <glm/glm.hpp>

namespace events::holeBrush
{
    struct SetHoleBrushParamsCommand : ICommand<>
    {
        ::terrain::HoleBrushParams params;

        std::string_view getName() const override { return "SetHoleBrushParams"; }
    };

    struct SetHoleBrushRadiusCommand : ICommand<>
    {
        float radius;

        std::string_view getName() const override { return "SetHoleBrushRadius"; }
    };

    struct SetHoleBrushFalloffCommand : ICommand<>
    {
        ::terrain::BrushFalloff falloff;

        std::string_view getName() const override { return "SetHoleBrushFalloff"; }
    };

    struct SetHoleBrushShapeCommand : ICommand<>
    {
        ::terrain::BrushShape shape;

        std::string_view getName() const override { return "SetHoleBrushShape"; }
    };

    struct GetHoleBrushParamsQuery : IQuery<::terrain::HoleBrushParams>
    {
        std::string_view getName() const override { return "GetHoleBrushParams"; }
    };

    struct ApplyHoleBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        bool erase = false;

        std::string_view getName() const override { return "ApplyHoleBrush"; }
    };

    struct HoleBrushParamsChangedNotification : INotification
    {
        ::terrain::HoleBrushParams params;

        std::string_view getName() const override { return "HoleBrushParamsChanged"; }
    };

    struct HoleBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "HoleBrushApplied"; }
    };
}
