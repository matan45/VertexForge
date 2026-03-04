#pragma once

#include "providers/animation/IAnimationPreviewProvider.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace editor { class OrbitCamera; }

namespace windows::animation
{
    class AnimationSkeletonPanel
    {
    public:
        void draw(const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                  const std::unordered_map<int32_t, std::vector<size_t>>& boneChildrenMap,
                  int& selectedChannel,
                  bool& showBoneVisualization,
                  bool meshLoadedInPreview,
                  editor::OrbitCamera* camera,
                  const std::unordered_set<std::string>* mappedBoneNames = nullptr);

    private:
        void drawBoneNode(size_t index,
                          const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                          const std::unordered_map<int32_t, std::vector<size_t>>& boneChildrenMap,
                          int& selectedChannel,
                          const std::unordered_set<std::string>* mappedBoneNames = nullptr);
    };
}
