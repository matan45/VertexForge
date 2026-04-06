#pragma once
#include "../EventTypes.hpp"
#include "../../utilities/terrain/BrushTypes.hpp"
#include "../../utilities/terrain/HeightmapLoader.hpp"
#include <glm/glm.hpp>
#include <string>
#include <memory>

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

    struct ClearStampImageCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearStampImage"; }
    };

    struct SetStampImageCommand : ICommand<>
    {
        std::string filePath;

        std::string_view getName() const override { return "SetStampImage"; }
    };

    struct SetStampRotationCommand : ICommand<>
    {
        float rotation = 0.0f;

        std::string_view getName() const override { return "SetStampRotation"; }
    };

    struct SetStampScaleCommand : ICommand<>
    {
        float scale = 1.0f;

        std::string_view getName() const override { return "SetStampScale"; }
    };

    struct SetStampModeCommand : ICommand<>
    {
        bool subtract = false;

        std::string_view getName() const override { return "SetStampMode"; }
    };

    struct SetTalusAngleCommand : ICommand<>
    {
        float angle = 45.0f;

        std::string_view getName() const override { return "SetTalusAngle"; }
    };

    struct StampImageChangedNotification : INotification
    {
        std::string filePath;
        bool loaded = false;

        std::string_view getName() const override { return "StampImageChanged"; }
    };

    struct GetStampDataQuery : IQuery<std::shared_ptr<::terrain::HeightmapData>>
    {
        std::string_view getName() const override { return "GetStampData"; }
    };
}
