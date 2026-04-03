#pragma once
#include "DestructionTypes.hpp"
#include <vector>

namespace destruction
{
    struct FragmentHullConfig
    {
        uint32_t maxVerticesPerHull = 32;
        uint32_t resolution = 50000;
        bool shrinkWrap = true;
    };

    class ConvexHullGenerator
    {
    public:
        static resource::ConvexHull generate(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            const FragmentHullConfig& config = {});

        static void generateBatch(
            std::vector<FragmentData>& fragments,
            const FragmentHullConfig& config = {});
    };
}
