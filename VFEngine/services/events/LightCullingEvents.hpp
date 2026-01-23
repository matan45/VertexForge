#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <vector>
#include <cstdint>

namespace events::lighting
{
    // Light type enumeration
    enum class LightType : uint8_t
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    // Statistics for light BVH culling
    struct LightCullingStats
    {
        size_t staticLightCount = 0;
        size_t dynamicLightCount = 0;
        size_t staticNodeCount = 0;
        size_t dynamicNodeCount = 0;
        size_t visibleLightCount = 0;
    };

    // Notification when light data changes (position, color, intensity, etc.)
    struct LightDataChangedNotification : INotification
    {
        services::EntityHandle entity;
        LightType lightType;

        std::string_view getName() const override { return "LightDataChanged"; }
    };

    // Notification when a light component is added or removed
    struct LightComponentChangedNotification : INotification
    {
        services::EntityHandle entity;
        LightType lightType;
        bool added;  // true = added, false = removed

        std::string_view getName() const override { return "LightComponentChanged"; }
    };

    // Notification when a light's static flag changes (via TransformComponent)
    struct LightStaticChangedNotification : INotification
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "LightStaticChanged"; }
    };

    // Query for visible lights in frustum
    struct GetVisibleLightsQuery : IQuery<std::vector<services::EntityHandle>>
    {
        // Uses the current camera frustum if no specific frustum is provided

        std::string_view getName() const override { return "GetVisibleLights"; }
    };

    // Query for light culling statistics
    struct GetLightCullingStatsQuery : IQuery<LightCullingStats>
    {
        std::string_view getName() const override { return "GetLightCullingStats"; }
    };

    // Command to force rebuild of the light BVH
    struct RebuildLightBVHCommand : ICommand<>
    {
        bool rebuildStatic = true;
        bool rebuildDynamic = true;

        std::string_view getName() const override { return "RebuildLightBVH"; }
    };

    // Command to mark a specific light as dirty (needs bounds update)
    struct MarkLightDirtyCommand : ICommand<>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "MarkLightDirty"; }
    };
}
