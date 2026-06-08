#pragma once
#include <cstddef>
#include <vector>
#include "../EventTypes.hpp"
#include "../../data/PluginTextureTypes.hpp"

namespace events::plugintexture {

    struct CreateTexture2DCommand : ICommand<plugin::PluginTextureHandle> {
        uint32_t width = 0;
        uint32_t height = 0;
        plugin::TextureFormat format = plugin::TextureFormat::R8;

        std::string_view getName() const override { return "CreateTexture2D"; }
    };

    struct UpdateTexture2DCommand : ICommand<> {
        plugin::PluginTextureHandle handle;
        mutable std::vector<std::byte> data;

        std::string_view getName() const override { return "UpdateTexture2D"; }
    };

    struct DestroyTexture2DCommand : ICommand<> {
        plugin::PluginTextureHandle handle;

        std::string_view getName() const override { return "DestroyTexture2D"; }
    };

    struct BindWorldMaskCommand : ICommand<> {
        plugin::PluginTextureHandle handle;
        glm::vec3 worldMin{0.0f};
        glm::vec3 worldMax{0.0f};
        plugin::WorldMaskParams params;

        std::string_view getName() const override { return "BindWorldMask"; }
    };

    struct UnbindWorldMaskCommand : ICommand<> {
        std::string_view getName() const override { return "UnbindWorldMask"; }
    };

    struct SetWorldMaskParamsCommand : ICommand<> {
        plugin::WorldMaskParams params;

        std::string_view getName() const override { return "SetWorldMaskParams"; }
    };

    // CPU readback of the bound world mask at a world (x,z) position. Returns the
    // red channel in [0,1]; 1.0 when no mask is bound, the mask is disabled, or
    // the point is outside the mask bounds (matches the shader semantics).
    struct SampleWorldMaskQuery : IQuery<float> {
        float worldX = 0.0f;
        float worldZ = 0.0f;

        std::string_view getName() const override { return "SampleWorldMask"; }
    };

}
