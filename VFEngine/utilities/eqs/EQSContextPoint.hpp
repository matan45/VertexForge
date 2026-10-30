#pragma once

#include "EQSTypes.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace eqs
{
    // Where a generator is centered, or where a test measures from or to (the template's "center", "to" and
    // "from" params, VK-1660).
    enum class EQSContextPoint : uint8_t
    {
        Querier,
        Target
    };

    // The point's world position. False with `error` set ("<user> needs a target") when it is the target and
    // the context has none.
    inline bool resolveContextPoint(const EQSContext& context, EQSContextPoint point, glm::vec3& position,
                                    std::string& error, std::string_view user)
    {
        if (point == EQSContextPoint::Target)
        {
            if (!context.hasTarget)
            {
                error = std::string(user) + " needs a target (the query was run without one)";
                return false;
            }
            position = context.targetPosition;
            return true;
        }
        position = context.querierPosition;
        return true;
    }

    // The entity at the point: the querier's or the target's (kNoEntity for a point target).
    inline uint64_t contextPointEntity(const EQSContext& context, EQSContextPoint point)
    {
        return point == EQSContextPoint::Target ? context.targetEntityId : context.querierEntityId;
    }
}
