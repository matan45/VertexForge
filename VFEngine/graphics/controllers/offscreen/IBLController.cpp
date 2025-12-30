#include "IBLController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/IBL.hpp"

namespace controllers::offscreen
{
    IBLController::IBLController(render::RenderPassHandler& renderHandler)
        : renderHandler{renderHandler}
    {
    }

    void IBLController::set(std::string_view iblPath)
    {
        renderHandler.getIBL()->init(iblPath);

        if (renderHandler.isMeshPipelineInitialized())
        {
            renderHandler.reinitMeshPipelineWithIBL();
        }
    }

    void IBLController::setCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        renderHandler.getIBL()->setCameraMatrices(view, projection);
    }

    void IBLController::remove()
    {
        renderHandler.getIBL()->remove();

        if (renderHandler.isMeshPipelineInitialized())
        {
            renderHandler.reinitMeshPipelineWithDefaults();
        }
    }
}
