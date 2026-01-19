#pragma once

#include "providers/IAnimationPreviewProvider.hpp"
#include <glm/glm.hpp>
#include <imgui.h>
#include <vector>

namespace editor { class OrbitCamera; }
namespace services { struct PreviewInstanceId; }

namespace windows::animation
{
    class AnimationViewport
    {
    public:
        void draw(float width, float height,
                  bool meshLoadedInPreview,
                  bool animationLoadedInPreview,
                  bool isPlaying,
                  editor::OrbitCamera* camera,
                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                  int selectedChannel,
                  bool showBoneVisualization,
                  const services::PreviewInstanceId& instanceId,
                  bool& isDraggingPreview);

    private:
        void handlePreviewInput(editor::OrbitCamera* camera, bool& isDraggingPreview);
        void drawBoneVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize,
                                   const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                   int selectedChannel,
                                   const editor::OrbitCamera* camera);
        void drawPlaceholder(const ImVec2& windowPos, const ImVec2& availSize);
        ImVec2 worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos,
                             const ImVec2& viewportSize, const editor::OrbitCamera* camera) const;
    };
}
