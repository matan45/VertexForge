#pragma once
#include "../resource/Types.hpp"

namespace geometry {

    struct SphereParams {
        float radius = 1.0f;
        uint32_t latitudeSegments = 32;  
        uint32_t longitudeSegments = 32;
    };

    class SphereGenerator {
    public:
        static resource::MeshData generate(const SphereParams& params = {});
        
        static resource::MeshesData generateMeshesData(const SphereParams& params = {});
    };

}
