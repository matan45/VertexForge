#include "RenderTextureServiceImpl.hpp"
#include "../../events/render/RenderTextureEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include <cassert>

namespace services
{
    RenderTextureServiceImpl::RenderTextureServiceImpl(IRenderTextureProvider* provider)
        : provider(provider)
    {
        assert(provider && "RenderTextureServiceImpl requires a valid IRenderTextureProvider");
    }

    void RenderTextureServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::rendertexture::CreateRenderTextureCommand>(
            [this](const events::rendertexture::CreateRenderTextureCommand& cmd)
            {
                return createRenderTexture(cmd.desc);
            });

        dispatcher.registerCommandHandler<events::rendertexture::DestroyRenderTextureCommand>(
            [this](const events::rendertexture::DestroyRenderTextureCommand& cmd)
            {
                destroyRenderTexture(cmd.textureId);
            });

        dispatcher.registerCommandHandler<events::rendertexture::UpdateRenderTextureCameraCommand>(
            [this](const events::rendertexture::UpdateRenderTextureCameraCommand& cmd)
            {
                updateCamera(cmd.textureId, cmd.view, cmd.projection,
                             cmd.cameraPos, cmd.nearPlane, cmd.farPlane, cmd.cullingMask);
            });

        dispatcher.registerCommandHandler<events::rendertexture::ResizeRenderTextureCommand>(
            [this](const events::rendertexture::ResizeRenderTextureCommand& cmd)
            {
                resize(cmd.textureId, cmd.width, cmd.height);
            });

        dispatcher.registerCommandHandler<events::rendertexture::SetRenderTextureEnabledCommand>(
            [this](const events::rendertexture::SetRenderTextureEnabledCommand& cmd)
            {
                setEnabled(cmd.textureId, cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::rendertexture::RequestRenderTextureRenderCommand>(
            [this](const events::rendertexture::RequestRenderTextureRenderCommand& cmd)
            {
                requestRender(cmd.textureId);
            });

        dispatcher.registerCommandHandler<events::rendertexture::RenderAllTexturesCommand>(
            [this](const events::rendertexture::RenderAllTexturesCommand& cmd)
            {
                renderAll(cmd.deltaTime);
            });

        dispatcher.registerQueryHandler<events::rendertexture::GetRenderTextureHandleQuery>(
            [this](const events::rendertexture::GetRenderTextureHandleQuery& query)
            {
                return getTextureHandle(query.textureId);
            });

        dispatcher.registerQueryHandler<events::rendertexture::IsRenderTextureValidQuery>(
            [this](const events::rendertexture::IsRenderTextureValidQuery& query)
            {
                return isValid(query.textureId);
            });

        dispatcher.registerQueryHandler<events::rendertexture::GetActiveRenderTexturesQuery>(
            [this](const events::rendertexture::GetActiveRenderTexturesQuery&)
            {
                return getActiveRenderTextures();
            });
    }

    rendertexture::RenderTextureId RenderTextureServiceImpl::createRenderTexture(
        const rendertexture::RenderTextureDesc& desc)
    {
        return provider->createRenderTexture(desc);
    }

    void RenderTextureServiceImpl::destroyRenderTexture(rendertexture::RenderTextureId id)
    {
        provider->destroyRenderTexture(id);
    }

    void RenderTextureServiceImpl::updateCamera(rendertexture::RenderTextureId id,
        const glm::mat4& view, const glm::mat4& proj,
        const glm::vec3& pos, float nearPlane, float farPlane, uint32_t cullingMask)
    {
        provider->updateCamera(id, view, proj, pos, nearPlane, farPlane, cullingMask);
    }

    void RenderTextureServiceImpl::renderAll(float deltaTime)
    {
        provider->renderAll(deltaTime);
    }

    ViewportTextureHandle RenderTextureServiceImpl::getTextureHandle(
        rendertexture::RenderTextureId id) const
    {
        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = provider->getTextureHandle(id);
        if (handle.imguiDescriptorSet)
        {
            handle.width = provider->getWidth(id);
            handle.height = provider->getHeight(id);
        }
        return handle;
    }

    bool RenderTextureServiceImpl::isValid(rendertexture::RenderTextureId id) const
    {
        return provider->isValid(id);
    }

    void RenderTextureServiceImpl::resize(rendertexture::RenderTextureId id, uint32_t w, uint32_t h)
    {
        provider->resize(id, w, h);
    }

    void RenderTextureServiceImpl::setEnabled(rendertexture::RenderTextureId id, bool enabled)
    {
        provider->setEnabled(id, enabled);
    }

    void RenderTextureServiceImpl::setUpdateMode(rendertexture::RenderTextureId id,
                                                  rendertexture::UpdateMode mode)
    {
        provider->setUpdateMode(id, mode);
    }

    void RenderTextureServiceImpl::requestRender(rendertexture::RenderTextureId id)
    {
        provider->requestRender(id);
    }

    std::vector<services::RenderTextureDebugInfo> RenderTextureServiceImpl::getActiveRenderTextures() const
    {
        return provider->getActiveRenderTextures();
    }
}
