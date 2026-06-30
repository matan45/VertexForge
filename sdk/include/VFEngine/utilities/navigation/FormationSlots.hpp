#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>
#include <span>
#include <vector>

namespace navigation
{
    enum class FormationKind : uint8_t
    {
        Grid = 0,
        Line = 1,
        Wedge = 2,
        Column = 3,
        Box = 4
    };

    struct FormationParams
    {
        FormationKind kind = FormationKind::Grid;
        float spacing = 2.0f;
    };

    inline FormationKind formationKindFromByte(uint8_t value)
    {
        switch (value)
        {
        case 1: return FormationKind::Line;
        case 2: return FormationKind::Wedge;
        case 3: return FormationKind::Column;
        case 4: return FormationKind::Box;
        default: return FormationKind::Grid;
        }
    }

    inline glm::vec3 gridSlotLocal(int index, int count, float spacing)
    {
        if (count <= 0 || index < 0)
            return glm::vec3(0.0f);

        int cols = 1;
        while (cols * cols < count)
            ++cols;

        const int rows = (count + cols - 1) / cols;
        const int row = index / cols;
        const int col = index % cols;
        const float x = (static_cast<float>(col) - (static_cast<float>(cols) - 1.0f) * 0.5f) * spacing;
        const float z = (static_cast<float>(row) - (static_cast<float>(rows) - 1.0f) * 0.5f) * spacing;
        return {x, 0.0f, z};
    }

    inline glm::vec3 formationSlotLocal(FormationKind kind, int index, int count, float spacing)
    {
        if (count <= 0 || index < 0)
            return glm::vec3(0.0f);

        switch (kind)
        {
        case FormationKind::Line:
        {
            const float x = (static_cast<float>(index) - (static_cast<float>(count) - 1.0f) * 0.5f) * spacing;
            return {x, 0.0f, 0.0f};
        }
        case FormationKind::Column:
        {
            const float z = (static_cast<float>(index) - (static_cast<float>(count) - 1.0f) * 0.5f) * spacing;
            return {0.0f, 0.0f, z};
        }
        case FormationKind::Wedge:
        {
            if (index == 0)
                return glm::vec3(0.0f);

            int row = 1;
            int rowStart = 1;
            while (index >= rowStart + row * 2)
            {
                rowStart += row * 2;
                ++row;
            }

            const int inRow = index - rowStart;
            const int rank = inRow / 2 + 1;
            const float side = (inRow % 2 == 0) ? -1.0f : 1.0f;
            return {side * static_cast<float>(rank) * spacing, 0.0f, -static_cast<float>(row) * spacing};
        }
        case FormationKind::Box:
        {
            if (count <= 4)
                return formationSlotLocal(FormationKind::Line, index, count, spacing);

            int side = 2;
            while (side * side < count)
                ++side;

            const int perimeter = std::max(1, side * 4 - 4);
            const int p = index % perimeter;
            const float half = (static_cast<float>(side) - 1.0f) * 0.5f * spacing;

            if (p < side)
                return {-half + static_cast<float>(p) * spacing, 0.0f, -half};
            if (p < side * 2 - 1)
                return {half, 0.0f, -half + static_cast<float>(p - side + 1) * spacing};
            if (p < side * 3 - 2)
                return {half - static_cast<float>(p - (side * 2 - 1) + 1) * spacing, 0.0f, half};
            return {-half, 0.0f, half - static_cast<float>(p - (side * 3 - 2) + 1) * spacing};
        }
        case FormationKind::Grid:
        default:
            return gridSlotLocal(index, count, spacing);
        }
    }

    inline glm::vec3 toWorld(const glm::vec3& slotLocal, const glm::vec3& center, float facingYawRad)
    {
        const float c = glm::cos(facingYawRad);
        const float s = glm::sin(facingYawRad);
        return {
            center.x + c * slotLocal.x + s * slotLocal.z,
            center.y + slotLocal.y,
            center.z - s * slotLocal.x + c * slotLocal.z
        };
    }

    inline glm::vec3 toLocalPlanar(const glm::vec3& world, const glm::vec3& center, float facingYawRad)
    {
        const glm::vec3 d = world - center;
        const float c = glm::cos(facingYawRad);
        const float s = glm::sin(facingYawRad);
        return {
            c * d.x - s * d.z,
            d.y,
            s * d.x + c * d.z
        };
    }

    inline void assignSlotsStable(std::span<const glm::vec3> unitPositions,
                                  const glm::vec3& center,
                                  float facingYawRad,
                                  const FormationParams& params,
                                  std::span<int> outSlotForUnit)
    {
        const int count = static_cast<int>(unitPositions.size());
        if (count <= 0 || outSlotForUnit.size() < unitPositions.size())
            return;

        struct OrderedIndex
        {
            int index = 0;
            glm::vec3 local{0.0f};
        };

        std::vector<OrderedIndex> units;
        std::vector<OrderedIndex> slots;
        units.reserve(count);
        slots.reserve(count);

        for (int i = 0; i < count; ++i)
        {
            units.push_back({i, toLocalPlanar(unitPositions[static_cast<size_t>(i)], center, facingYawRad)});
            slots.push_back({i, formationSlotLocal(params.kind, i, count, params.spacing)});
        }

        auto rowMajorLess = [](const OrderedIndex& a, const OrderedIndex& b)
        {
            if (a.local.z == b.local.z)
                return a.local.x < b.local.x;
            return a.local.z < b.local.z;
        };

        std::stable_sort(units.begin(), units.end(), rowMajorLess);
        std::stable_sort(slots.begin(), slots.end(), rowMajorLess);

        for (int i = 0; i < count; ++i)
            outSlotForUnit[static_cast<size_t>(units[static_cast<size_t>(i)].index)] = slots[static_cast<size_t>(i)].index;
    }
}
