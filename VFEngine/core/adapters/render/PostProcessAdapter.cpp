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

    events::postprocess::UpscaleStatus PostProcessAdapter::getUpscaleStatus() const
    {
        events::postprocess::UpscaleStatus status{};
        // Upscale status will be populated once UpscaleManager is fully integrated
        // into the OffScreen rendering pipeline. For now, report Streamline availability.
#ifdef VF_STREAMLINE_ENABLED
        status.streamlineAvailable = render::upscaling::UpscaleManager::isStreamlineAvailable();
#endif
        return status;
    }
}
