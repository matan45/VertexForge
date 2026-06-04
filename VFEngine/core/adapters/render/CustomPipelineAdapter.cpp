#include "CustomPipelineAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../../graphics/render/RenderPassHandler.hpp"

namespace core
{
    CustomPipelineAdapter::CustomPipelineAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen)
    {
    }

    plugin::CustomPipelineHandle CustomPipelineAdapter::createPipeline(const plugin::CustomPipelineDesc& desc)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return {};
        return handler->createCustomPipeline(desc);
    }

    plugin::CustomMeshHandle CustomPipelineAdapter::uploadMesh(plugin::CustomMeshData&& data)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return {};
        return handler->uploadCustomMesh(std::move(data));
    }

    void CustomPipelineAdapter::enqueueDraw(plugin::CustomDrawItem&& item)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->enqueueCustomDraw(std::move(item));
    }

    void CustomPipelineAdapter::destroyPipeline(plugin::CustomPipelineHandle handle)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->destroyCustomPipeline(handle);
    }

    void CustomPipelineAdapter::destroyMesh(plugin::CustomMeshHandle handle)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->destroyCustomMesh(handle);
    }
}
