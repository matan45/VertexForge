#include "ColliderOverlayRenderer.hpp"
#include "../../camera/OrbitCamera.hpp"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace windows::animation
{
    void ColliderOverlayRenderer::draw(ImDrawList* drawList,
                                        const ImVec2& viewportPos, const ImVec2& viewportSize,
                                        const editor::OrbitCamera* camera,
                                        const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                        const types::PhysicsAnimationConfig& config,
                                        const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                        int selectedChannel)
    {
        if (!drawList || !camera || evaluatedBones.empty()) return;

        ImU32 normalColor = IM_COL32(0, 255, 0, 180);
        ImU32 selectedColor = IM_COL32(0, 255, 255, 255);

        for (const auto& mapping : config.boneBodyMappings)
        {
            auto it = boneNameToIndex.find(mapping.boneName);
            if (it == boneNameToIndex.end()) continue;

            size_t boneIndex = it->second;
            if (boneIndex >= evaluatedBones.size()) continue;

            const auto& bone = evaluatedBones[boneIndex];

            glm::mat4 boneWorld = bone.worldTransform;
            glm::vec3 bonePos = glm::vec3(boneWorld[3]);
            glm::quat boneRot = bone.rotation;

            glm::vec3 colliderCenter = glm::vec3(boneWorld * glm::vec4(mapping.offset, 1.0f));
            glm::quat colliderRot = boneRot * mapping.rotationOffset;

            bool isSelected = (static_cast<int>(boneIndex) == selectedChannel);
            ImU32 color = isSelected ? selectedColor : normalColor;
            float thickness = isSelected ? 2.5f : 1.5f;

            switch (mapping.shape)
            {
            case types::ColliderShape::Box:
                drawWireBox(drawList, colliderCenter, mapping.size, colliderRot,
                            viewportPos, viewportSize, camera, color, thickness);
                break;
            case types::ColliderShape::Sphere:
                drawWireSphere(drawList, colliderCenter, mapping.size.x, colliderRot,
                               viewportPos, viewportSize, camera, color, thickness);
                break;
            case types::ColliderShape::Capsule:
                drawWireCapsule(drawList, colliderCenter, mapping.size.x, mapping.size.y, colliderRot,
                                viewportPos, viewportSize, camera, color, thickness);
                break;
            default:
                break;
            }
        }
    }

    ImVec2 ColliderOverlayRenderer::worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos,
                                                   const ImVec2& viewportSize, const editor::OrbitCamera* camera) const
    {
        glm::vec4 clipPos = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(worldPos, 1.0f);

        if (std::abs(clipPos.w) < 0.0001f)
        {
            return ImVec2(-10000, -10000);
        }

        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

        float screenX = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        float screenY = viewportPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;

        return ImVec2(screenX, screenY);
    }

    bool ColliderOverlayRenderer::isInFrontOfCamera(const glm::vec3& worldPos, const editor::OrbitCamera* camera) const
    {
        glm::vec4 clipPos = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(worldPos, 1.0f);
        return clipPos.w > 0.001f;
    }

    void ColliderOverlayRenderer::drawWireBox(ImDrawList* drawList,
                                               const glm::vec3& center, const glm::vec3& halfExtents,
                                               const glm::quat& rotation,
                                               const ImVec2& viewportPos, const ImVec2& viewportSize,
                                               const editor::OrbitCamera* camera,
                                               ImU32 color, float thickness)
    {
        glm::vec3 axes[3] = {
            rotation * glm::vec3(halfExtents.x, 0, 0),
            rotation * glm::vec3(0, halfExtents.y, 0),
            rotation * glm::vec3(0, 0, halfExtents.z)
        };

        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i)
        {
            corners[i] = center
                + axes[0] * ((i & 1) ? 1.0f : -1.0f)
                + axes[1] * ((i & 2) ? 1.0f : -1.0f)
                + axes[2] * ((i & 4) ? 1.0f : -1.0f);
        }

        ImVec2 screenCorners[8];
        bool visible[8];
        for (int i = 0; i < 8; ++i)
        {
            visible[i] = isInFrontOfCamera(corners[i], camera);
            screenCorners[i] = worldToScreen(corners[i], viewportPos, viewportSize, camera);
        }

        // 12 edges of a box
        static const int edges[12][2] = {
            {0,1}, {2,3}, {4,5}, {6,7},  // x-axis edges
            {0,2}, {1,3}, {4,6}, {5,7},  // y-axis edges
            {0,4}, {1,5}, {2,6}, {3,7}   // z-axis edges
        };

        for (const auto& edge : edges)
        {
            if (visible[edge[0]] && visible[edge[1]])
            {
                drawList->AddLine(screenCorners[edge[0]], screenCorners[edge[1]], color, thickness);
            }
        }
    }

    void ColliderOverlayRenderer::drawWireSphere(ImDrawList* drawList,
                                                  const glm::vec3& center, float radius,
                                                  const glm::quat& rotation,
                                                  const ImVec2& viewportPos, const ImVec2& viewportSize,
                                                  const editor::OrbitCamera* camera,
                                                  ImU32 color, float thickness)
    {
        glm::vec3 axisX = rotation * glm::vec3(1, 0, 0);
        glm::vec3 axisY = rotation * glm::vec3(0, 1, 0);
        glm::vec3 axisZ = rotation * glm::vec3(0, 0, 1);

        constexpr int segments = 32;
        drawCircle(drawList, center, axisX, axisY, radius, segments, viewportPos, viewportSize, camera, color, thickness);
        drawCircle(drawList, center, axisX, axisZ, radius, segments, viewportPos, viewportSize, camera, color, thickness);
        drawCircle(drawList, center, axisY, axisZ, radius, segments, viewportPos, viewportSize, camera, color, thickness);
    }

    void ColliderOverlayRenderer::drawWireCapsule(ImDrawList* drawList,
                                                   const glm::vec3& center, float radius, float halfHeight,
                                                   const glm::quat& rotation,
                                                   const ImVec2& viewportPos, const ImVec2& viewportSize,
                                                   const editor::OrbitCamera* camera,
                                                   ImU32 color, float thickness)
    {
        glm::vec3 axisX = rotation * glm::vec3(1, 0, 0);
        glm::vec3 axisY = rotation * glm::vec3(0, 1, 0);
        glm::vec3 axisZ = rotation * glm::vec3(0, 0, 1);

        glm::vec3 topCenter = center + axisY * halfHeight;
        glm::vec3 botCenter = center - axisY * halfHeight;

        constexpr int segments = 24;

        // Top and bottom circles
        drawCircle(drawList, topCenter, axisX, axisZ, radius, segments, viewportPos, viewportSize, camera, color, thickness);
        drawCircle(drawList, botCenter, axisX, axisZ, radius, segments, viewportPos, viewportSize, camera, color, thickness);

        // 4 longitudinal lines
        for (int i = 0; i < 4; ++i)
        {
            float angle = static_cast<float>(i) * glm::half_pi<float>();
            glm::vec3 offset = (axisX * std::cos(angle) + axisZ * std::sin(angle)) * radius;

            glm::vec3 top = topCenter + offset;
            glm::vec3 bot = botCenter + offset;

            if (isInFrontOfCamera(top, camera) && isInFrontOfCamera(bot, camera))
            {
                ImVec2 topScreen = worldToScreen(top, viewportPos, viewportSize, camera);
                ImVec2 botScreen = worldToScreen(bot, viewportPos, viewportSize, camera);
                drawList->AddLine(topScreen, botScreen, color, thickness);
            }
        }

        // Top hemisphere arcs (XY and ZY planes)
        constexpr float halfPi = glm::half_pi<float>();
        drawArc(drawList, topCenter, axisX, axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);
        drawArc(drawList, topCenter, axisZ, axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);
        drawArc(drawList, topCenter, -axisX, axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);
        drawArc(drawList, topCenter, -axisZ, axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);

        // Bottom hemisphere arcs
        drawArc(drawList, botCenter, axisX, -axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);
        drawArc(drawList, botCenter, axisZ, -axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);
        drawArc(drawList, botCenter, -axisX, -axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);
        drawArc(drawList, botCenter, -axisZ, -axisY, radius, 0.0f, halfPi, segments / 2,
                viewportPos, viewportSize, camera, color, thickness);
    }

    void ColliderOverlayRenderer::drawCircle(ImDrawList* drawList,
                                              const glm::vec3& center, const glm::vec3& axis1, const glm::vec3& axis2,
                                              float radius, int segments,
                                              const ImVec2& viewportPos, const ImVec2& viewportSize,
                                              const editor::OrbitCamera* camera,
                                              ImU32 color, float thickness)
    {
        drawArc(drawList, center, axis1, axis2, radius, 0.0f, glm::two_pi<float>(), segments,
                viewportPos, viewportSize, camera, color, thickness);
    }

    void ColliderOverlayRenderer::drawArc(ImDrawList* drawList,
                                           const glm::vec3& center, const glm::vec3& axis1, const glm::vec3& axis2,
                                           float radius, float startAngle, float endAngle, int segments,
                                           const ImVec2& viewportPos, const ImVec2& viewportSize,
                                           const editor::OrbitCamera* camera,
                                           ImU32 color, float thickness)
    {
        float step = (endAngle - startAngle) / static_cast<float>(segments);

        glm::vec3 prevPos = center + (axis1 * std::cos(startAngle) + axis2 * std::sin(startAngle)) * radius;
        bool prevVisible = isInFrontOfCamera(prevPos, camera);
        ImVec2 prevScreen = prevVisible ? worldToScreen(prevPos, viewportPos, viewportSize, camera) : ImVec2(-10000, -10000);

        for (int i = 1; i <= segments; ++i)
        {
            float angle = startAngle + step * static_cast<float>(i);
            glm::vec3 pos = center + (axis1 * std::cos(angle) + axis2 * std::sin(angle)) * radius;
            bool curVisible = isInFrontOfCamera(pos, camera);
            ImVec2 curScreen = curVisible ? worldToScreen(pos, viewportPos, viewportSize, camera) : ImVec2(-10000, -10000);

            if (prevVisible && curVisible)
            {
                drawList->AddLine(prevScreen, curScreen, color, thickness);
            }

            prevPos = pos;
            prevVisible = curVisible;
            prevScreen = curScreen;
        }
    }
}
