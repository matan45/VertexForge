#pragma once
#include "../../services/providers/render/IRenderTextureProvider.hpp"
#include "../../graphics/core/GraphicsConstants.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace controllers
{
    class OffScreen;
    class RenderTextureController;
}

namespace render
{
    class RenderPassHandler;
}

namespace core
{
    class RenderTextureAdapter : public services::IRenderTextureProvider
    {
    private:
        std::unordered_map<rendertexture::RenderTextureId,
            std::unique_ptr<::controllers::RenderTextureController>> controllers;
        rendertexture::RenderTextureId nextId = 1;

        ::controllers::OffScreen* mainOffScreen = nullptr;

        // Serializes mutation of `controllers` (create/destroy/resize from the main thread on
        // play-mode transitions and user resize) against iteration on the render thread inside
        // renderAll(). Without this, Stop-button → exitPlayMode → destroyRenderTexture can erase
        // an entry while the render thread holds a raw RenderTextureController* in its local
        // toRender/enabled vectors → UAF.
        mutable std::mutex controllersMutex;

        // VK-1418: per-RTT dedicated scene-bindless slots, one per swapchain image, so an RTT can
        // drive a 3D material slot (albedo/emission) without write-after-read flicker (each image's
        // slot is only ever pointed at that image's view). Reserved lazily in renderAll, freed on
        // destroy/resize. Entries default to INVALID_TEXTURE_INDEX until first registered.
        std::unordered_map<rendertexture::RenderTextureId,
            std::array<uint32_t, ::core::MAX_SWAPCHAIN_IMAGES>> rttMaterialBindlessSlots;

    public:
        explicit RenderTextureAdapter(::controllers::OffScreen* offScreen);
        ~RenderTextureAdapter() noexcept override;

        RenderTextureAdapter(const RenderTextureAdapter&) = delete;
        RenderTextureAdapter& operator=(const RenderTextureAdapter&) = delete;

        rendertexture::RenderTextureId createRenderTexture(
            const rendertexture::RenderTextureDesc& desc) override;
        void destroyRenderTexture(rendertexture::RenderTextureId id) override;

        void updateCamera(rendertexture::RenderTextureId id,
            const glm::mat4& view, const glm::mat4& proj,
            const glm::vec3& pos, float nearPlane, float farPlane,
            uint32_t cullingMask = 0xFFFFFFFFu) override;

        void renderAll(float deltaTime) override;
        void* getTextureHandle(rendertexture::RenderTextureId id) const override;
        uint32_t getWidth(rendertexture::RenderTextureId id) const override;
        uint32_t getHeight(rendertexture::RenderTextureId id) const override;
        bool isValid(rendertexture::RenderTextureId id) const override;

        void resize(rendertexture::RenderTextureId id, uint32_t w, uint32_t h) override;
        void setEnabled(rendertexture::RenderTextureId id, bool enabled) override;
        void setUpdateMode(rendertexture::RenderTextureId id,
                           rendertexture::UpdateMode mode) override;
        void requestRender(rendertexture::RenderTextureId id) override;

        std::vector<services::RenderTextureDebugInfo> getActiveRenderTextures() const override;

    private:
        ::controllers::RenderTextureController* getController(rendertexture::RenderTextureId id) const;
        render::RenderPassHandler* getMainRenderPassHandler() const;

        // VK-1418: unregister this RTT's per-image scene-bindless material slots (keyed
        // "<key>#<imageIndex>") and drop the map entry. Caller must ensure no in-flight
        // submission references the slots (controller cleanUp/resize waitIdle).
        void releaseMaterialBindlessSlots(render::RenderPassHandler* passHandler,
                                          rendertexture::RenderTextureId id,
                                          const std::string& key);
    };
}
