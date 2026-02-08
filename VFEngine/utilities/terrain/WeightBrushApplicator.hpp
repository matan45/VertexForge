#pragma once

#include "TerrainWeightMap.hpp"
#include "PaintBrushTypes.hpp"
#include <glm/glm.hpp>

namespace terrain
{
    class WeightBrushApplicator
    {
    public:
        struct ApplyParams
        {
            glm::vec2 brushCenter;       // World XZ position of brush center
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float brushStrength;
            float brushOpacity;
            float vertexSpacing;
            uint32_t verticesPerSide;
            BrushFalloff falloff;
            BrushShape shape;
            PaintBrushType brushType;
            uint32_t activeLayer;
            float deltaTime;
            bool invert;
            uint16_t overlayMask = 0;    // Bitmask: bit N = 1 means layer N uses Overlay blend
        };

        // Apply brush to a single tile's weight map. Returns true if any texels were modified.
        static bool apply(TileWeightMapData& weightMap, const ApplyParams& params);

    private:
        // Compute normalized distance [0,1] from brush center.
        // Returns >= 1.0 if outside brush radius.
        static float computeNormalizedDistance(
            const glm::vec2& texelWorldPos,
            const glm::vec2& brushCenter,
            float brushRadius,
            BrushShape shape);

        // Apply falloff curve matching GPU shader (brush_influence.glsl).
        // Input t is normalized distance [0,1]. Returns influence [0,1].
        static float applyFalloff(float t, BrushFalloff falloff);

        static bool isOverlay(uint16_t mask, uint32_t layer) { return layer < 16 && (mask & (1u << layer)) != 0; }

        static void paintLayer(TileWeightMapData& wm, uint32_t x, uint32_t z,
                               uint32_t layer, float influence, uint16_t overlayMask);
        static void eraseLayer(TileWeightMapData& wm, uint32_t x, uint32_t z,
                               uint32_t layer, float influence, uint16_t overlayMask);
        static void smoothWeights(TileWeightMapData& wm, uint32_t x, uint32_t z,
                                  float influence, uint16_t overlayMask);
        static void fillLayer(TileWeightMapData& wm, uint32_t x, uint32_t z,
                              uint32_t layer, float influence, uint16_t overlayMask);
    };
}
