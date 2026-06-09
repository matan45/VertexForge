#include "PostProcessAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../graphics/render/upscaling/UpscaleManager.hpp"

namespace core
{
    PostProcessAdapter::PostProcessAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen)
    {
    }

    void PostProcessAdapter::applyPostProcessSettings(const postprocess::PostProcessSettings& settings)
    {
        if (offScreen)
        {
            offScreen->applyPostProcessSettings(settings);
        }
    }

    postprocess::PostProcessSettings PostProcessAdapter::getPostProcessSettings() const
    {
        return offScreen ? offScreen->getPostProcessSettings() : postprocess::PostProcessSettings{};
    }

    void PostProcessAdapter::setPostProcessEnabled(bool enabled)
    {
        if (offScreen)
        {
            offScreen->setPostProcessEnabled(enabled);
        }
    }

    bool PostProcessAdapter::isPostProcessEnabled() const
    {
        return offScreen && offScreen->isPostProcessEnabled();
    }

    services::UpscaleStatus PostProcessAdapter::getUpscaleStatus() const
    {
        services::UpscaleStatus status{};
        status.streamlineAvailable = render::upscaling::UpscaleManager::isStreamlineAvailable();
        status.dlssSupported = render::upscaling::UpscaleManager::isDLSSAvailable();
        status.directSRSupported = render::upscaling::UpscaleManager::isDirectSRAvailable();
        auto* mgr = render::upscaling::UpscaleManager::getInstance();
        if (mgr)
        {
            status.activeMode = mgr->getActiveMode();
            const auto& res = mgr->getResolutionManager();
            status.renderWidth = res.getRenderWidth();
            status.renderHeight = res.getRenderHeight();
            status.displayWidth = res.getDisplayWidth();
            status.displayHeight = res.getDisplayHeight();
            status.dlssGSupported = mgr->isDLSSGSupported();
            status.frameGenActive = mgr->isFrameGenActive();

            status.reflexSupported = mgr->isReflexSupported();
            status.reflexActive = mgr->isReflexActive();
            status.reflexMode = mgr->getActiveReflexMode();

            // getUpscaleStatus runs on the ImGui/main thread, so the non-thread-safe
            // slReflexGetState inside getLatency() is safe here.
            auto latency = mgr->getLatency();
            status.latencyValid = latency.valid;
            status.gpuFrameTimeUs = latency.gpuFrameTimeUs;
            status.totalLatencyUs = latency.totalLatencyUs;
        }
        return status;
    }
}
