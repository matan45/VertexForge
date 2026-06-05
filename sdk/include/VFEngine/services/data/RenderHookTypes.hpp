#pragma once
#include <cstdint>
#include <functional>

namespace plugin {

    enum class RenderPassHookPoint : uint32_t {
        PostScene       = 0,   // After scene meshes + VFX, before overlays
        PrePostProcess  = 1,   // After overlays + occlusion + volumetric fog, before post-process
        PostPostProcess = 2,   // After post-processing, before UI overlays
        Overlay         = 3    // After UI overlays (final layer)
    };

    struct RenderHookHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    // Forward declaration - full definition in RenderHookContext.hpp (requires Vulkan headers)
    struct RenderHookContext;

    using RenderHookCallback = std::function<void(const RenderHookContext&)>;

}
