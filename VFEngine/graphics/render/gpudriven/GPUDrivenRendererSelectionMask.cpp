#include "GPUDrivenRenderer.hpp"
#include "SelectionMaskPipeline.hpp"
#include "../../core/SwapChain.hpp"

namespace render::gpudriven
{
    bool GPUDrivenRenderer::ensureSelectionMaskResources()
    {
        if (!initialized || !enabled || !selectionMaskPipeline ||
            !selectionMaskPipeline->isInitialized())
        {
            return false;
        }

        selectionMaskPipeline->ensureMaskTarget(swapChain.getSwapchainExtent());
        return selectionMaskPipeline->getMaskImageView() != nullptr;
    }
}
