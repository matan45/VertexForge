#pragma once

#include "providers/IAnimationPreviewProvider.hpp"
#include "animator/IKTypes.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace windows::animation
{
    class AnimationIKChainPanel
    {
    private:
        int selectedChainIndex = -1;
        char newChainName[64] = "NewChain";
        int chainLength = 3;
        bool saveSuccess = false;
        float saveMessageTimer = 0.0f;

    public:
        bool draw(std::vector<animator::ik::IKChainConfig>& chains,
                  int& selectedChannel,
                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                  const std::unordered_map<std::string, size_t>& boneNameToIndex,
                  const std::unordered_map<int32_t, std::vector<size_t>>& boneChildrenMap,
                  bool& showIKVisualization,
                  const std::string& meshPath = "");

    private:
        void drawChainList(std::vector<animator::ik::IKChainConfig>& chains);
        bool drawChainEditor(animator::ik::IKChainConfig& chain,
                             const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                             const std::unordered_map<std::string, size_t>& boneNameToIndex);
        bool drawNewChainCreation(std::vector<animator::ik::IKChainConfig>& chains,
                                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                  const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                  int selectedChannel);
        void drawConstraintEditor(animator::ik::JointConstraint& constraint, int boneIdx);
        void drawSaveButton(const std::vector<animator::ik::IKChainConfig>& chains,
                            const std::string& meshPath);

        std::vector<std::string> walkHierarchyUp(int tipBoneIndex, int length,
                                                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones);
    };
}
