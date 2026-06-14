#pragma once
#include "../EventTypes.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/AsyncLoadingTypes.hpp"
#include "../../providers/render/IMaterialPreviewProvider.hpp" // services::MaterialPreviewParams
#include <string>
#include <cstdint>

// VK-1379: rendered Content Browser thumbnails (mesh / material / material
// instance). The editor-side AssetThumbnailCache speaks the same async pattern
// as the editor-texture path, but routes through a small pool of reusable
// preview controllers (ThumbnailRenderAdapter) that GPU-blit each render into an
// owned texture. Handlers live in core::ThumbnailRenderAdapter::init().
namespace events::render {

    enum class RenderThumbnailKind : uint8_t { Mesh, Material };

    struct LoadRenderThumbnailAsyncCommand : ICommand<> {
        void* instanceId = nullptr;
        RenderThumbnailKind kind = RenderThumbnailKind::Mesh;
        std::string meshPath;                          // kind == Mesh
        services::MaterialPreviewParams materialParams; // kind == Material

        std::string_view getName() const override { return "LoadRenderThumbnailAsync"; }
    };

    struct CancelRenderThumbnailCommand : ICommand<> {
        void* instanceId = nullptr;

        std::string_view getName() const override { return "CancelRenderThumbnail"; }
    };

    struct ReleaseRenderThumbnailCommand : ICommand<> {
        void* handle = nullptr;

        std::string_view getName() const override { return "ReleaseRenderThumbnail"; }
    };

    struct GetRenderThumbnailProgressQuery : IQuery<services::TextureLoadingProgress> {
        void* instanceId = nullptr;

        std::string_view getName() const override { return "GetRenderThumbnailProgress"; }
    };

    struct GetRenderThumbnailHandleQuery : IQuery<services::EditorTextureHandle> {
        void* instanceId = nullptr;

        std::string_view getName() const override { return "GetRenderThumbnailHandle"; }
    };

}
