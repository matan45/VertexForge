#pragma once

#include "providers/IAnimationPreviewProvider.hpp"
#include "animator/SocketTypes.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace windows::animation
{
    class AnimationSocketPanel
    {
    private:
        int selectedSocketIndex = -1;
        char newSocketName[128] = "";
        bool saveSuccess = false;
        float saveMessageTimer = 0.0f;
        
    public:
        bool draw(std::vector<animator::SocketDefinition>& sockets,
                  int& selectedChannel,
                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                  const std::unordered_map<std::string, size_t>& boneNameToIndex,
                  bool& showSocketVisualization,
                  const std::string& meshPath = "");

    private:
        void drawSocketList(std::vector<animator::SocketDefinition>& sockets, int& selectedSocket);
        bool drawSocketEditor(animator::SocketDefinition& socket,
                              const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                              const std::unordered_map<std::string, size_t>& boneNameToIndex);
        bool drawNewSocketCreation(std::vector<animator::SocketDefinition>& sockets,
                                   const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                   const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                   int selectedChannel);
        void drawSaveButton(const std::vector<animator::SocketDefinition>& sockets,
                            const std::string& meshPath);
    };
}
