#pragma once
#include "../EventTypes.hpp"
#include "../../utilities/terrain/PaintBrushTypes.hpp"
#include <glm/glm.hpp>

namespace events::paintBrush
{
    struct SetPaintBrushParamsCommand : ICommand<>
    {
        ::terrain::PaintBrushParams params;

        std::string_view getName() const override { return "SetPaintBrushParams"; }
    };

    struct SetPaintBrushRadiusCommand : ICommand<>
    {
        float radius;

        std::string_view getName() const override { return "SetPaintBrushRadius"; }
    };

    struct SetPaintBrushStrengthCommand : ICommand<>
    {
        float strength;

        std::string_view getName() const override { return "SetPaintBrushStrength"; }
    };

    struct SetPaintBrushOpacityCommand : ICommand<>
    {
        float opacity;

        std::string_view getName() const override { return "SetPaintBrushOpacity"; }
    };

    struct SetPaintActiveLayerCommand : ICommand<>
    {
        uint32_t layer;

        std::string_view getName() const override { return "SetPaintActiveLayer"; }
    };

    struct SetPaintBrushFalloffCommand : ICommand<>
    {
        ::terrain::BrushFalloff falloff;

        std::string_view getName() const override { return "SetPaintBrushFalloff"; }
    };

    struct SetPaintBrushShapeCommand : ICommand<>
    {
        ::terrain::BrushShape shape;

        std::string_view getName() const override { return "SetPaintBrushShape"; }
    };

    struct SetPaintBrushTypeCommand : ICommand<>
    {
        ::terrain::PaintBrushType type;

        std::string_view getName() const override { return "SetPaintBrushType"; }
    };

    struct ApplyPaintBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        float deltaTime = 0.0f;
        bool invert = false;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyPaintBrush"; }
    };

    struct GetPaintBrushParamsQuery : IQuery<::terrain::PaintBrushParams>
    {
        std::string_view getName() const override { return "GetPaintBrushParams"; }
    };

    struct GetPaintBrushTypeQuery : IQuery<::terrain::PaintBrushType>
    {
        std::string_view getName() const override { return "GetPaintBrushType"; }
    };

    struct PaintBrushParamsChangedNotification : INotification
    {
        ::terrain::PaintBrushParams params;

        std::string_view getName() const override { return "PaintBrushParamsChanged"; }
    };

    struct PaintBrushTypeChangedNotification : INotification
    {
        ::terrain::PaintBrushType type;

        std::string_view getName() const override { return "PaintBrushTypeChanged"; }
    };

    struct PaintBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        ::terrain::PaintBrushType type;

        std::string_view getName() const override { return "PaintBrushApplied"; }
    };
}
