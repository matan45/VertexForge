#pragma once

#include <cstdint>
#include <string>

namespace vfx
{
    // VK-1476 — per-emitter MESH-render-mode orientation. The integer values are
    // the on-GPU contract consumed by the shared mesh orientation function
    // (resources/shaders/vfx/vfx_mesh_orientation.glsl):
    //   0 = VelocityForward  mesh nose (+Z) follows velocity, roll about it   (legacy default)
    //   1 = Tumble           per-particle random spin axis from spawnSeed
    //   2 = AxisLock         spin about a fixed world-space axis (params.xyz)
    //   3 = CameraFacing     mesh faces the camera, roll about view axis
    // VelocityForward reproduces the pre-VK-1476 behavior exactly (byte-identical
    // default). Do NOT renumber — the value flows straight into the GPU config's
    // `meshOrientationMode` uint and the mesh shader branches on it.
    enum class VFXOrientationMode : uint8_t
    {
        VelocityForward = 0,
        Tumble = 1,
        AxisLock = 2,
        CameraFacing = 3
    };

    inline const char* orientationModeToString(VFXOrientationMode mode)
    {
        switch (mode)
        {
        case VFXOrientationMode::VelocityForward: return "velocityForward";
        case VFXOrientationMode::Tumble:          return "tumble";
        case VFXOrientationMode::AxisLock:        return "axisLock";
        case VFXOrientationMode::CameraFacing:    return "cameraFacing";
        default:                                  return "velocityForward";
        }
    }

    inline VFXOrientationMode stringToOrientationMode(const std::string& str)
    {
        if (str == "tumble")       return VFXOrientationMode::Tumble;
        if (str == "axisLock")     return VFXOrientationMode::AxisLock;
        if (str == "cameraFacing") return VFXOrientationMode::CameraFacing;
        return VFXOrientationMode::VelocityForward;
    }

    // The value written to the GPU `meshOrientationMode` uint (0..3). Kept as a
    // function so the enum-to-uint contract has a single, testable home.
    inline uint32_t orientationModeToGpuValue(VFXOrientationMode mode)
    {
        return static_cast<uint32_t>(mode);
    }
}
