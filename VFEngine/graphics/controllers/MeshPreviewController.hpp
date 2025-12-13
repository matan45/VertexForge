#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include "../../services/data/DTOs.hpp"
#include <memory>
#include <string>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
}

namespace imguiPass
{
    class OffScreenViewPort;
}

namespace controllers
{
    class MeshPreviewController
    {
    private:
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<imguiPass::OffScreenViewPort> offScreen;

        std::string loadedMeshPath;
        math::AABB meshBounds;
        math::Frustum currentFrustum;
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
        bool initialized = false;

    public:
        MeshPreviewController();
        ~MeshPreviewController();

        void init();
        void cleanUp();

        // Load a mesh and get its bounding box for camera fitting
        // Returns true if mesh was loaded successfully
        bool loadMesh(const std::string& meshPath, math::AABB& outBounds);
        void unloadMesh();

        // Get submesh information for the loaded mesh
        std::vector<services::SubMeshInfo> getSubMeshInfo() const;

        // Set which submesh to highlight (-1 = none)
        void setHighlightedSubMesh(int index) { highlightedSubMesh = index; }
        int getHighlightedSubMesh() const { return highlightedSubMesh; }

        // Set mesh transform
        void setModelMatrix(const glm::mat4& matrix) { modelMatrix = matrix; }

        // Update camera matrices before rendering
        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                         const glm::vec3& cameraPos);

        // Render and return the ImGui texture handle
        void* render();

        const math::AABB& getMeshBounds() const { return meshBounds; }
        bool isMeshLoaded() const { return !loadedMeshPath.empty(); }
    };
}
