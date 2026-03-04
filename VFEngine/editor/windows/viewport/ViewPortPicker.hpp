#pragma once
#include "data/EntityHandle.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <string>

namespace editor
{
    class EditorCamera;
}

namespace windows
{
    struct BillboardScreenHit
    {
        services::EntityHandle entity;
        glm::vec2 screenCenter;
        glm::vec2 screenSize;
    };

    struct MeshPickData
    {
        services::EntityHandle entity;
        math::AABB worldAABB;
        std::string meshPath;
    };

    class ViewPortPicker
    {
    private:
        std::vector<BillboardScreenHit> cachedBillboardHits;
        std::vector<MeshPickData> cachedMeshHits;

    public:
        void updateBillboardScreenPositions(const editor::EditorCamera& camera,
                                            glm::vec2 viewportPos, glm::vec2 viewportSize);
        std::optional<services::EntityHandle> pickBillboardAt(glm::vec2 screenPos);

        void updateMeshPickData();
        std::optional<services::EntityHandle> pickMeshAt(const editor::EditorCamera& camera,
                                                         glm::vec2 screenPos,
                                                         glm::vec2 viewportPos,
                                                         glm::vec2 viewportSize);

    private:
        math::Ray screenToWorldRay(const editor::EditorCamera& camera,
                                   glm::vec2 screenPos,
                                   glm::vec2 viewportPos,
                                   glm::vec2 viewportSize);
    };
}
