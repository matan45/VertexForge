#pragma once

#include "../BrushTypes.hpp"
#include <glm/glm.hpp>
#include <functional>

namespace terrain
{
    class TerrainTile;

    namespace brushes
    {
        struct BrushContext
        {
            glm::vec2 brushCenter{0.0f};
            BrushParams params;
            float deltaTime = 0.0f;
            bool invert = false;
            float targetHeight = 0.0f;
            std::function<float(float worldX, float worldZ)> sampleWorldHeight;
        };

        class ISculptBrush
        {
        public:
            virtual ~ISculptBrush() = default;

            [[nodiscard]] virtual BrushType getType() const = 0;

            virtual void apply(TerrainTile& tile, const BrushContext& context) = 0;
        };
    }
}
