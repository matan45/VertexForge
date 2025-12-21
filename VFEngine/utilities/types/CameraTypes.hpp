#pragma once
#include <cstdint>

namespace types {

    using CameraId = uint32_t;

    constexpr CameraId MAIN_CAMERA_ID = 0;
    constexpr CameraId INVALID_CAMERA_ID = UINT32_MAX;

}
