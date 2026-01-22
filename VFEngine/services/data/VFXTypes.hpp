#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace services
{
    using VFXInstanceId = uint32_t;

    struct VFXRuntimeParams
    {
        std::string vfxAssetPath;
        glm::mat4 worldTransform{1.0f};
        bool loop = true;
    };
}
