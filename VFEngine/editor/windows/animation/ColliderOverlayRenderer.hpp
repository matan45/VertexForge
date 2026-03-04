#pragma once

#include "providers/animation/IAnimationPreviewProvider.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>
#include <imgui.h>
#include <vector>
#include <string>
#include <unordered_map>

namespace editor { class OrbitCamera; }

namespace windows::animation
{
    class ColliderOverlayRenderer
    {
    public:
        void draw(ImDrawList* drawList,
                  const ImVec2& viewportPos, const ImVec2& viewportSize,
                  const editor::OrbitCamera* camera,
                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                  const types::PhysicsAnimationConfig& config,
                  const std::unordered_map<std::string, size_t>& boneNameToIndex,
                  int selectedChannel);

    private:
        ImVec2 worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos,
                             const ImVec2& viewportSize, const editor::OrbitCamera* camera) const;

        bool isInFrontOfCamera(const glm::vec3& worldPos, const editor::OrbitCamera* camera) const;

        void drawWireBox(ImDrawList* drawList,
                         const glm::vec3& center, const glm::vec3& halfExtents,
                         const glm::quat& rotation,
                         const ImVec2& viewportPos, const ImVec2& viewportSize,
                         const editor::OrbitCamera* camera,
                         ImU32 color, float thickness);

        void drawWireSphere(ImDrawList* drawList,
                            const glm::vec3& center, float radius,
                            const glm::quat& rotation,
                            const ImVec2& viewportPos, const ImVec2& viewportSize,
                            const editor::OrbitCamera* camera,
                            ImU32 color, float thickness);

        void drawWireCapsule(ImDrawList* drawList,
                             const glm::vec3& center, float radius, float halfHeight,
                             const glm::quat& rotation,
                             const ImVec2& viewportPos, const ImVec2& viewportSize,
                             const editor::OrbitCamera* camera,
                             ImU32 color, float thickness);

        void drawCircle(ImDrawList* drawList,
                        const glm::vec3& center, const glm::vec3& axis1, const glm::vec3& axis2,
                        float radius, int segments,
                        const ImVec2& viewportPos, const ImVec2& viewportSize,
                        const editor::OrbitCamera* camera,
                        ImU32 color, float thickness);

        void drawArc(ImDrawList* drawList,
                     const glm::vec3& center, const glm::vec3& axis1, const glm::vec3& axis2,
                     float radius, float startAngle, float endAngle, int segments,
                     const ImVec2& viewportPos, const ImVec2& viewportSize,
                     const editor::OrbitCamera* camera,
                     ImU32 color, float thickness);
    };
}
