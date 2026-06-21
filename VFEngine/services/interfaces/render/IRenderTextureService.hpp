#pragma once
#include <rendertexture/RenderTextureTypes.hpp>
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace services
{
    class IRenderTextureService
    {
    public:
        virtual ~IRenderTextureService() = default;

        virtual rendertexture::RenderTextureId createRenderTexture(
            const rendertexture::RenderTextureDesc& desc) = 0;
        virtual void destroyRenderTexture(rendertexture::RenderTextureId id) = 0;

        virtual void updateCamera(rendertexture::RenderTextureId id,
            const glm::mat4& view, const glm::mat4& proj,
            const glm::vec3& pos, float nearPlane, float farPlane,
            uint32_t cullingMask = 0xFFFFFFFFu) = 0;

        virtual void renderAll(float deltaTime) = 0;

        [[nodiscard]] virtual ViewportTextureHandle getTextureHandle(
            rendertexture::RenderTextureId id) const = 0;
        [[nodiscard]] virtual bool isValid(rendertexture::RenderTextureId id) const = 0;

        virtual void resize(rendertexture::RenderTextureId id, uint32_t w, uint32_t h) = 0;
        virtual void setEnabled(rendertexture::RenderTextureId id, bool enabled) = 0;
        virtual void setUpdateMode(rendertexture::RenderTextureId id,
                                   rendertexture::UpdateMode mode) = 0;
        virtual void requestRender(rendertexture::RenderTextureId id) = 0;

        [[nodiscard]] virtual std::vector<services::RenderTextureDebugInfo> getActiveRenderTextures() const = 0;

        virtual void registerEventHandlers() = 0;
    };
}
