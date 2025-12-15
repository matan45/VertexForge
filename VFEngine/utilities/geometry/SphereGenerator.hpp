#pragma once
#include "../resource/Types.hpp"

namespace geometry {

    struct SphereParams {
        float radius = 1.0f;
        uint32_t latitudeSegments = 32;   // Horizontal rings
        uint32_t longitudeSegments = 32;  // Vertical slices
    };

    class SphereGenerator {
    public:
        // Generate sphere mesh data
        static resource::MeshData generate(const SphereParams& params = {});

        // Generate and return as MeshesData (ready for pipeline upload)
        static resource::MeshesData generateMeshesData(const SphereParams& params = {});
    };

}
