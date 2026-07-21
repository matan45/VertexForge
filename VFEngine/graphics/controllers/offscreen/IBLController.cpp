#include "IBLController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/IBL.hpp"

namespace controllers::offscreen
{
    IBLController::IBLController(render::RenderPassHandler& renderHandler)
        : renderHandler{renderHandler}
    {
    }

    IBLController::~IBLController() = default;

    void IBLController::set(std::string_view iblPath)
    {
        // VK-1574: non-blocking HDR IBL bake. The old blocking IBL::init (~73 serialized submits) is
        // replaced by HdrEnvironmentCapture: the first bind does one blocking full capture (scene
        // load already blocks), subsequent applies ride the per-frame time-sliced capture and the
        // previous environment stays visible until the new bake publishes (no hitch, no gray flash).
        renderHandler.applyHdrEnvironment(iblPath);
    }

    void IBLController::setCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        renderHandler.getIBL()->setCameraMatrices(view, projection);
    }

    void IBLController::remove()
    {
        renderHandler.removeHdrEnvironment();
    }
}
