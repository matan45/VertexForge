#pragma once

#include "NavmeshData.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>

namespace navigation
{
    struct CorridorProjection
    {
        float arcLength = 0.0f;
        glm::vec3 point{0.0f};
        int segmentIndex = -1;
    };

    struct CorridorFollowParams
    {
        float lookAhead = 2.0f;
        float arriveRadius = 1.5f;
        float lateralOffset = 0.0f;
        float corridorHalfWidth = 4.0f;
    };

    inline glm::vec3 planar(const glm::vec3& v)
    {
        return {v.x, 0.0f, v.z};
    }

    inline float corridorSegmentLength(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length(planar(b - a));
    }

    inline float corridorTotalLength(const NavPath& corridor)
    {
        if (corridor.waypoints.size() < 2)
            return 0.0f;

        float total = 0.0f;
        for (size_t i = 1; i < corridor.waypoints.size(); ++i)
            total += corridorSegmentLength(corridor.waypoints[i - 1], corridor.waypoints[i]);
        return total;
    }

    inline CorridorProjection projectOntoCorridor(const NavPath& corridor, const glm::vec3& p)
    {
        CorridorProjection best;
        best.point = p;

        if (corridor.waypoints.empty())
            return best;
        if (corridor.waypoints.size() == 1)
        {
            best.point = corridor.waypoints.front();
            return best;
        }

        float bestDistSq = std::numeric_limits<float>::max();
        float arcBase = 0.0f;
        for (size_t i = 1; i < corridor.waypoints.size(); ++i)
        {
            const glm::vec3 a = corridor.waypoints[i - 1];
            const glm::vec3 b = corridor.waypoints[i];
            const glm::vec3 ab = planar(b - a);
            const float lenSq = glm::dot(ab, ab);
            const float t = (lenSq > 1e-6f)
                ? std::clamp(glm::dot(planar(p - a), ab) / lenSq, 0.0f, 1.0f)
                : 0.0f;
            const glm::vec3 q = a + (b - a) * t;
            const float distSq = glm::dot(planar(p - q), planar(p - q));
            const float segLen = glm::sqrt(lenSq);

            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                best.point = q;
                best.arcLength = arcBase + segLen * t;
                best.segmentIndex = static_cast<int>(i - 1);
            }

            arcBase += segLen;
        }

        return best;
    }

    inline glm::vec3 corridorPointAtArcLength(const NavPath& corridor, float arcLength)
    {
        if (corridor.waypoints.empty())
            return glm::vec3(0.0f);
        if (corridor.waypoints.size() == 1 || arcLength <= 0.0f)
            return corridor.waypoints.front();

        float remaining = arcLength;
        for (size_t i = 1; i < corridor.waypoints.size(); ++i)
        {
            const glm::vec3 a = corridor.waypoints[i - 1];
            const glm::vec3 b = corridor.waypoints[i];
            const float len = corridorSegmentLength(a, b);
            if (len <= 1e-5f)
                continue;
            if (remaining <= len)
            {
                const float t = remaining / len;
                return a + (b - a) * t;
            }
            remaining -= len;
        }

        return corridor.waypoints.back();
    }

    inline glm::vec3 corridorTangentAtArcLength(const NavPath& corridor, float arcLength)
    {
        if (corridor.waypoints.size() < 2)
            return {0.0f, 0.0f, 1.0f};

        float remaining = std::max(0.0f, arcLength);
        for (size_t i = 1; i < corridor.waypoints.size(); ++i)
        {
            const glm::vec3 a = corridor.waypoints[i - 1];
            const glm::vec3 b = corridor.waypoints[i];
            const float len = corridorSegmentLength(a, b);
            if (len <= 1e-5f)
                continue;
            if (remaining <= len)
                return planar(b - a) / len;
            remaining -= len;
        }

        const glm::vec3 a = corridor.waypoints[corridor.waypoints.size() - 2];
        const glm::vec3 b = corridor.waypoints.back();
        const float len = corridorSegmentLength(a, b);
        return (len > 1e-5f) ? planar(b - a) / len : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    inline glm::vec3 corridorFollowVelocity(const NavPath& corridor,
                                            const glm::vec3& memberPos,
                                            float maxSpeed,
                                            CorridorFollowParams params = {})
    {
        if (!corridor.isValid || corridor.waypoints.empty() || maxSpeed <= 0.0f)
            return glm::vec3(0.0f);

        const float total = corridorTotalLength(corridor);
        const CorridorProjection projection = projectOntoCorridor(corridor, memberPos);
        const float remaining = std::max(0.0f, total - projection.arcLength);
        if (remaining <= params.arriveRadius)
            return glm::vec3(0.0f);

        const float carrotArc = std::min(total, projection.arcLength + std::max(0.0f, params.lookAhead));
        glm::vec3 carrot = corridorPointAtArcLength(corridor, carrotArc);
        const glm::vec3 tangent = corridorTangentAtArcLength(corridor, carrotArc);
        const glm::vec3 right = {tangent.z, 0.0f, -tangent.x};
        const float lateral = std::clamp(params.lateralOffset, -params.corridorHalfWidth, params.corridorHalfWidth);
        carrot += right * lateral;

        glm::vec3 desired = planar(carrot - memberPos);
        float dist = glm::length(desired);
        if (dist <= 1e-4f)
        {
            desired = planar(corridor.waypoints.back() - memberPos);
            dist = glm::length(desired);
        }
        if (dist <= 1e-4f)
            return glm::vec3(0.0f);

        const float slowRadius = std::max(params.arriveRadius * 2.0f, 1e-3f);
        const float speedScale = std::clamp(remaining / slowRadius, 0.2f, 1.0f);
        return desired / dist * maxSpeed * speedScale;
    }

    inline bool corridorStale(uint64_t builtVersion, uint64_t currentVersion)
    {
        return builtVersion != currentVersion;
    }
}
