#include "PostProcessAdapter.hpp"
#include "../controllers/OffScreen.hpp"

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
}
