#pragma once
#include "../EventTypes.hpp"
#include "../../data/DTOs.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
#include <glm/glm.hpp>

namespace services::events::rendertexture {

    // ============================================================
    // COMMANDS
    // ============================================================

    struct CreateRenderTextureCommand : ::events::ICommand<::rendertexture::RenderTextureId> {
        ::rendertexture::RenderTextureDesc desc;
        std::string_view getName() const override { return "CreateRenderTexture"; }
    };

    struct DestroyRenderTextureCommand : ::events::ICommand<void> {
        ::rendertexture::RenderTextureId textureId;
        std::string_view getName() const override { return "DestroyRenderTexture"; }
    };

    struct UpdateRenderTextureCameraCommand : ::events::ICommand<void> {
        ::rendertexture::RenderTextureId textureId;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        std::string_view getName() const override { return "UpdateRenderTextureCamera"; }
    };

    struct ResizeRenderTextureCommand : ::events::ICommand<void> {
        ::rendertexture::RenderTextureId textureId;
        uint32_t width;
        uint32_t height;
        std::string_view getName() const override { return "ResizeRenderTexture"; }
    };

    struct SetRenderTextureEnabledCommand : ::events::ICommand<void> {
        ::rendertexture::RenderTextureId textureId;
        bool enabled;
        std::string_view getName() const override { return "SetRenderTextureEnabled"; }
    };

    struct RequestRenderTextureRenderCommand : ::events::ICommand<void> {
        ::rendertexture::RenderTextureId textureId;
        std::string_view getName() const override { return "RequestRenderTextureRender"; }
    };

    struct RenderAllTexturesCommand : ::events::ICommand<void> {
        float deltaTime;
        std::string_view getName() const override { return "RenderAllTextures"; }
    };

    // ============================================================
    // QUERIES
    // ============================================================

    struct GetRenderTextureHandleQuery : ::events::IQuery<ViewportTextureHandle> {
        ::rendertexture::RenderTextureId textureId;
        std::string_view getName() const override { return "GetRenderTextureHandle"; }
    };

    struct IsRenderTextureValidQuery : ::events::IQuery<bool> {
        ::rendertexture::RenderTextureId textureId;
        std::string_view getName() const override { return "IsRenderTextureValid"; }
    };

}
