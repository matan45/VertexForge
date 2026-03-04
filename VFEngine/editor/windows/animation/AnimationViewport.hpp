#pragma once

#include "providers/animation/IAnimationPreviewProvider.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "ColliderOverlayRenderer.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include "animator/SocketTypes.hpp"
#include <glm/glm.hpp>
#include <imgui.h>
#include <vector>
#include <string>
#include <unordered_map>

namespace editor { class OrbitCamera; }

namespace windows::animation
{
    struct ViewportDrawContext
    {
        float width;
        float height;
        bool meshLoadedInPreview;
        bool animationLoadedInPreview;
        bool isPlaying;
        editor::OrbitCamera* camera;
        const std::vector<services::EvaluatedBoneInfo>& evaluatedBones;
        int selectedChannel;
        bool showBoneVisualization;
        const services::PreviewInstanceId& instanceId;
        bool& isDraggingPreview;
        bool showColliderOverlay = false;
        const types::PhysicsAnimationConfig* physicsConfig = nullptr;
        const std::unordered_map<std::string, size_t>* boneNameToIndex = nullptr;
        bool showSocketVisualization = false;
        const std::vector<animator::SocketDefinition>* socketDefinitions = nullptr;
    };

    class AnimationViewport
    {
    private:
        ColliderOverlayRenderer colliderOverlay;
    public:
        void draw(const ViewportDrawContext& ctx);

    private:
        void handlePreviewInput(editor::OrbitCamera* camera, bool& isDraggingPreview);
        void drawBoneVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize,
                                   const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                   int selectedChannel,
                                   const editor::OrbitCamera* camera);
        void drawSocketVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize,
                                     const ViewportDrawContext& ctx);
        void drawPlaceholder(const ImVec2& windowPos, const ImVec2& availSize);
        ImVec2 worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos,
                             const ImVec2& viewportSize, const editor::OrbitCamera* camera) const;
    };
}
