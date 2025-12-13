#include "MaterialPreviewController.hpp"
#include "../core/VulkanContext.hpp"
#include "../imguiPass/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "geometry/SphereGenerator.hpp"
#include "print/Logger.hpp"

namespace controllers
{
    MaterialPreviewController::MaterialPreviewController()
        : swapChain{ *core::VulkanContext::getSwapChain() }
        , device{ *core::VulkanContext::getDevice() }
        , offScreen{ std::make_unique<imguiPass::OffScreenViewPort>(device, swapChain) }
    {
    }

    MaterialPreviewController::~MaterialPreviewController()
    {
        device.getLogicalDevice().waitIdle();
        cleanUp();
    }

    void MaterialPreviewController::init()
    {
        if (initialized)
        {
            return;
        }

        offScreen->init();

        // Initialize mesh pipeline with default IBL textures
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->initMeshPipeline();

        // Generate and upload procedural sphere
        auto* meshPipeline = renderHandler->getMeshPipeline();
        if (meshPipeline)
        {
            geometry::SphereParams sphereParams;
            sphereParams.radius = 1.0f;
            sphereParams.latitudeSegments = 32;
            sphereParams.longitudeSegments = 32;

            resource::MeshesData sphereData = geometry::SphereGenerator::generateMeshesData(sphereParams);
            std::string result = meshPipeline->uploadMesh(SPHERE_MESH_ID, sphereData);
            sphereLoaded = !result.empty();

            if (sphereLoaded)
            {
                loggerInfo("Material preview sphere created");
            }
            else
            {
                loggerError("Failed to create material preview sphere");
            }
        }

        initialized = true;
    }

    void MaterialPreviewController::cleanUp()
    {
        if (initialized && sphereLoaded)
        {
            auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
            if (meshPipeline)
            {
                meshPipeline->unloadMesh(SPHERE_MESH_ID);
            }
            sphereLoaded = false;
        }

        if (initialized && offScreen)
        {
            offScreen->cleanUp();
        }

        offScreen.reset();
        initialized = false;
    }

    void MaterialPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                                  const glm::vec3& cameraPos)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();

        if (renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->getMeshPipeline()->updateCameraUBO(view, projection, cameraPos);
        }

        // Update frustum for culling
        currentFrustum.extractFromMatrix(projection * view);
    }

    void* MaterialPreviewController::render()
    {
        if (!initialized || !sphereLoaded)
        {
            return nullptr;
        }

        auto* renderHandler = offScreen->getRenderPassHandler();

        // Create draw list with preview sphere
        std::vector<render::mesh::MeshRenderData> meshDrawList;

        render::mesh::MeshRenderData renderData;
        renderData.meshPath = SPHERE_MESH_ID;
        renderData.modelMatrix = glm::mat4(1.0f);  // Sphere at origin
        renderData.albedo = materialParams.albedo;
        renderData.metallic = materialParams.metallic;
        renderData.roughness = materialParams.roughness;
        renderData.ao = materialParams.ao;
        renderData.emission = materialParams.emission;
        renderData.showBoundingBox = false;
        renderData.highlightedSubMesh = -1;

        meshDrawList.push_back(renderData);

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&currentFrustum);

        // Render and return descriptor set
        vk::DescriptorSet descriptorSet = offScreen->render();
        return static_cast<void*>(descriptorSet);
    }
}
