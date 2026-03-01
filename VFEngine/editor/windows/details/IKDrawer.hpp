#pragma once
#include "data/EntityHandle.hpp"
#include <string>
#include <vector>

namespace animator::ik { struct JointConstraint; struct IKChainConfig; }

namespace windows::details
{
    class IKDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        void refreshBoneNames(services::EntityHandle handle);
        void drawChainEditor(animator::ik::IKChainConfig& chain, bool& isInitialized);
        void drawAddChainSection(services::EntityHandle handle);
        static void drawConstraintEditor(animator::ik::JointConstraint& constraint, int boneIdx);
        static bool drawBoneCombo(const char* label, std::string& boneName,
                                  const std::vector<std::string>& boneNames);

        char newChainName[64] = "NewChain";

        std::vector<std::string> boneNames;
        services::EntityHandle cachedEntity{};
    };
}
