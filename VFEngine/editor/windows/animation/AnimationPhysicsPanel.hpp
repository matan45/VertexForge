#pragma once

#include "providers/IAnimationPreviewProvider.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace windows::animation
{
    class AnimationPhysicsPanel
    {
    public:
        bool draw(types::PhysicsAnimationConfig& config,
                  int& selectedChannel,
                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                  const std::unordered_map<std::string, size_t>& boneNameToIndex,
                  bool& showColliderOverlay,
                  std::string& configPath);

    private:
        void drawFileBar(types::PhysicsAnimationConfig& config, std::string& configPath);
        bool drawGlobalConfig(types::PhysicsAnimationConfig& config, bool& showColliderOverlay);
        bool drawSelectedBoneMapping(types::PhysicsAnimationConfig& config,
                                     int selectedChannel,
                                     const std::vector<services::EvaluatedBoneInfo>& evaluatedBones);
        bool drawSelectedBoneJointLimits(types::PhysicsAnimationConfig& config,
                                         int selectedChannel,
                                         const std::vector<services::EvaluatedBoneInfo>& evaluatedBones);
        bool drawMappingFields(types::BoneBodyMapping& mapping);
        bool drawJointLimitFields(types::JointConstraintLimits& limits);
        bool drawLayerCombo(const char* id, uint8_t& layerValue);
        void drawAllMappingsSummary(const types::PhysicsAnimationConfig& config,
                                    int& selectedChannel,
                                    const std::unordered_map<std::string, size_t>& boneNameToIndex);
    };
}
