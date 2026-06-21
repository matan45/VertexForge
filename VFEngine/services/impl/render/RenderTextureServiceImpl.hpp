#pragma once
#include "../../interfaces/render/IRenderTextureService.hpp"
#include "../../providers/render/IRenderTextureProvider.hpp"

namespace services
{
    class RenderTextureServiceImpl : public IRenderTextureService
    {
    private:
        IRenderTextureProvider* provider;

    public:
        explicit RenderTextureServiceImpl(IRenderTextureProvider* provider);
        ~RenderTextureServiceImpl() override = default;

        rendertexture::RenderTextureId createRenderTexture(
            const rendertexture::RenderTextureDesc& desc) override;
        void destroyRenderTexture(rendertexture::RenderTextureId id) override;

        void updateCamera(rendertexture::RenderTextureId id,
            const glm::mat4& view, const glm::mat4& proj,
            const glm::vec3& pos, float nearPlane, float farPlane,
            uint32_t cullingMask = 0xFFFFFFFFu) override;

        void renderAll(float deltaTime) override;

        ViewportTextureHandle getTextureHandle(rendertexture::RenderTextureId id) const override;
        bool isValid(rendertexture::RenderTextureId id) const override;

        void resize(rendertexture::RenderTextureId id, uint32_t w, uint32_t h) override;
        void setEnabled(rendertexture::RenderTextureId id, bool enabled) override;
        void setUpdateMode(rendertexture::RenderTextureId id,
                           rendertexture::UpdateMode mode) override;
        void requestRender(rendertexture::RenderTextureId id) override;

        std::vector<services::RenderTextureDebugInfo> getActiveRenderTextures() const override;

        void registerEventHandlers() override;
    };
}
