#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string_view>
#include <optional>
#include "resource/ConvexHullTypes.hpp"

namespace core::physics {

    struct PhysicsMeshData {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        bool isValid() const { return !vertices.empty(); }
    };

    class PhysicsMeshLoader {
    public:
        static std::optional<PhysicsMeshData> loadFromFile(
            std::string_view meshPath,
            uint32_t lodLevel = 2,
            uint32_t submeshIndex = 0
        );

        static std::optional<PhysicsMeshData> loadAllSubmeshes(
            std::string_view meshPath,
            uint32_t lodLevel = 2
        );

        static std::optional<resource::ConvexDecompositionData> loadConvexDecomposition(
            std::string_view meshPath,
            uint32_t submeshIndex = 0
        );

        static std::optional<resource::ConvexDecompositionData> loadAllConvexDecompositions(
            std::string_view meshPath
        );
    };

}
