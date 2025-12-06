#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../imguiPass/OffScreenViewPort.hpp"
#include "../render/IBL.hpp"
#include "../render/RenderPassHandler.hpp"

namespace controllers
{
    OffScreenController::OffScreenController()
        : swapChain{ *core::VulkanContext::getSwapChain() }
        , device{ *core::VulkanContext::getDevice() }
        , offScreen{ std::make_unique<imguiPass::OffScreenViewPort>(device, swapChain) }
    {
    }

    OffScreenController::~OffScreenController() = default;

    void OffScreenController::init()
    {
        offScreen->init();
    }

    void OffScreenController::cleanUp() const
    {
        offScreen->cleanUp();
    }

    void OffScreenController::iblAdd(std::string_view iblPath, components::CameraComponent* camera)
    {
        render::IBL* ibl = offScreen->getRenderPassHandler()->getIBL();
        ibl->init(iblPath);
        ibl->setCamera(camera);
    }

    void OffScreenController::iblRemove()
    {
        offScreen->getRenderPassHandler()->getIBL()->remove();
    }

    void* OffScreenController::render()
    {
        return offScreen->render();
    }
}
