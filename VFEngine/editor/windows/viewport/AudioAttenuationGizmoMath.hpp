#pragma once

#include "math/Frustum.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <glm/glm.hpp>

namespace windows::audioattenuation
{
    enum class HandleKind
    {
        None,
        MinDistance,
        MaxDistance
    };

    struct Distances
    {
        float minDistance = 0.0f;
        float maxDistance = 0.0f;

        bool operator==(const Distances&) const = default;
    };

    struct HandleLayout
    {
        glm::vec2 minHandle{0.0f};
        glm::vec2 maxHandle{0.0f};
        glm::vec3 cameraRight{1.0f, 0.0f, 0.0f};
        glm::vec3 planeNormal{0.0f, 0.0f, -1.0f};
        bool minVisible = false;
        bool maxVisible = false;
    };

    inline bool isFinite(const glm::vec2& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    inline bool isFinite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    inline std::optional<glm::vec2> projectWorldToScreen(
        const glm::vec3& worldPosition,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec2& viewportPosition,
        const glm::vec2& viewportSize)
    {
        if (!isFinite(worldPosition) || !isFinite(viewportPosition) || !isFinite(viewportSize) ||
            viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return std::nullopt;
        }

        const glm::vec4 clip = projection * view * glm::vec4(worldPosition, 1.0f);
        constexpr float minClipW = 1e-6f;
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) ||
            clip.w <= minClipW)
        {
            return std::nullopt;
        }

        const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
        const glm::vec2 screen(
            (ndc.x * 0.5f + 0.5f) * viewportSize.x + viewportPosition.x,
            (ndc.y * 0.5f + 0.5f) * viewportSize.y + viewportPosition.y);
        return isFinite(screen) ? std::optional<glm::vec2>(screen) : std::nullopt;
    }

    inline bool isInsideViewport(const glm::vec2& point,
                                 const glm::vec2& viewportPosition,
                                 const glm::vec2& viewportSize)
    {
        return point.x >= viewportPosition.x && point.y >= viewportPosition.y &&
               point.x <= viewportPosition.x + viewportSize.x &&
               point.y <= viewportPosition.y + viewportSize.y;
    }

    inline std::optional<HandleLayout> buildHandleLayout(
        const glm::vec3& center,
        const Distances& distances,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec2& viewportPosition,
        const glm::vec2& viewportSize)
    {
        if (!isFinite(center) || !std::isfinite(distances.minDistance) ||
            !std::isfinite(distances.maxDistance) || distances.minDistance < 0.0f ||
            distances.maxDistance < distances.minDistance)
        {
            return std::nullopt;
        }

        const glm::mat4 cameraWorld = glm::inverse(view);
        glm::vec3 cameraRight(cameraWorld[0]);
        glm::vec3 planeNormal = -glm::vec3(cameraWorld[2]);
        const float rightLength = glm::length(cameraRight);
        const float normalLength = glm::length(planeNormal);
        constexpr float minAxisLength = 1e-6f;
        if (!std::isfinite(rightLength) || !std::isfinite(normalLength) ||
            rightLength <= minAxisLength || normalLength <= minAxisLength)
        {
            return std::nullopt;
        }
        cameraRight /= rightLength;
        planeNormal /= normalLength;

        const auto minScreen = projectWorldToScreen(
            center + cameraRight * distances.minDistance,
            view, projection, viewportPosition, viewportSize);
        const auto maxScreen = projectWorldToScreen(
            center - cameraRight * distances.maxDistance,
            view, projection, viewportPosition, viewportSize);
        if (!minScreen.has_value() && !maxScreen.has_value())
        {
            return std::nullopt;
        }

        HandleLayout layout;
        layout.cameraRight = cameraRight;
        layout.planeNormal = planeNormal;
        if (minScreen.has_value())
        {
            layout.minHandle = *minScreen;
            layout.minVisible = isInsideViewport(*minScreen, viewportPosition, viewportSize);
        }
        if (maxScreen.has_value())
        {
            layout.maxHandle = *maxScreen;
            layout.maxVisible = isInsideViewport(*maxScreen, viewportPosition, viewportSize);
        }
        return layout;
    }

    inline HandleKind hitTest(const HandleLayout& layout,
                              const glm::vec2& mousePosition,
                              float hitRadius)
    {
        if (!isFinite(mousePosition) || !std::isfinite(hitRadius) || hitRadius < 0.0f)
        {
            return HandleKind::None;
        }

        HandleKind closest = HandleKind::None;
        float closestDistanceSquared = hitRadius * hitRadius;
        if (layout.minVisible)
        {
            const glm::vec2 delta = mousePosition - layout.minHandle;
            const float distanceSquared = glm::dot(delta, delta);
            if (distanceSquared <= closestDistanceSquared)
            {
                closest = HandleKind::MinDistance;
                closestDistanceSquared = distanceSquared;
            }
        }
        if (layout.maxVisible)
        {
            const glm::vec2 delta = mousePosition - layout.maxHandle;
            const float distanceSquared = glm::dot(delta, delta);
            // Max wins an exact tie, making coincident zero-radius handles deterministic.
            if (distanceSquared <= closestDistanceSquared)
            {
                closest = HandleKind::MaxDistance;
            }
        }
        return closest;
    }

    inline std::optional<glm::vec3> intersectRayWithPlane(
        const math::Ray& ray,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal)
    {
        if (!isFinite(ray.origin) || !isFinite(ray.direction) || !isFinite(planePoint) ||
            !isFinite(planeNormal))
        {
            return std::nullopt;
        }

        const float denominator = glm::dot(ray.direction, planeNormal);
        constexpr float minDenominator = 1e-6f;
        if (!std::isfinite(denominator) || std::abs(denominator) <= minDenominator)
        {
            return std::nullopt;
        }

        const float distance = glm::dot(planePoint - ray.origin, planeNormal) / denominator;
        if (!std::isfinite(distance) || distance < 0.0f)
        {
            return std::nullopt;
        }

        const glm::vec3 intersection = ray.origin + ray.direction * distance;
        return isFinite(intersection) ? std::optional<glm::vec3>(intersection) : std::nullopt;
    }

    inline std::optional<float> radiusAlongAxis(const math::Ray& ray,
                                                const glm::vec3& center,
                                                const glm::vec3& planeNormal,
                                                const glm::vec3& outwardAxis)
    {
        const auto intersection = intersectRayWithPlane(ray, center, planeNormal);
        if (!intersection.has_value() || !isFinite(outwardAxis))
        {
            return std::nullopt;
        }

        const float radius = glm::dot(*intersection - center, outwardAxis);
        return std::isfinite(radius) ? std::optional<float>(radius) : std::nullopt;
    }

    inline std::optional<Distances> constrainDraggedDistance(HandleKind kind,
                                                             float candidate,
                                                             const Distances& current)
    {
        if (!std::isfinite(candidate) || !std::isfinite(current.minDistance) ||
            !std::isfinite(current.maxDistance))
        {
            return std::nullopt;
        }

        Distances result;
        switch (kind)
        {
        case HandleKind::MinDistance:
            result.maxDistance = std::max(0.0f, current.maxDistance);
            result.minDistance = std::clamp(candidate, 0.0f, result.maxDistance);
            break;
        case HandleKind::MaxDistance:
            result.minDistance = std::max(0.0f, current.minDistance);
            result.maxDistance = std::max(candidate, result.minDistance);
            break;
        case HandleKind::None:
            return std::nullopt;
        }
        return result;
    }
}
