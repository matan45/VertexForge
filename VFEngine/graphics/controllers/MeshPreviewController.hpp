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

namespace render
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
        std::unique_ptr<render::OffScreenViewPort> offScreen;

        std::string loadedMeshPath;
        math::AABB meshBounds;
        math::Frustum currentFrustum;
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
        int forceLODLevel = -1;       // -1 = auto, 0-3 = force specific LOD
        bool initialized = false;

    public:
        explicit MeshPreviewController();
        ~MeshPreviewController();

        void init();
        void cleanUp();

        
        bool loadMesh(const std::string& meshPath, math::AABB& outBounds);
        void unloadMesh();

        std::vector<services::SubMeshInfo> getSubMeshInfo() const;
        std::vector<services::LODInfo> getLODInfo() const;

        void setHighlightedSubMesh(int index) { highlightedSubMesh = index; }
        int getHighlightedSubMesh() const { return highlightedSubMesh; }

        void setForceLODLevel(int level) { forceLODLevel = level; }
        int getForceLODLevel() const { return forceLODLevel; }
        
        void setModelMatrix(const glm::mat4& matrix) { modelMatrix = matrix; }
        
        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                         const glm::vec3& cameraPos);
        
        void* render();

        const math::AABB& getMeshBounds() const { return meshBounds; }
        bool isMeshLoaded() const { return !loadedMeshPath.empty(); }
    };
}
