#pragma once

#include "animator/AnimatorTypes.hpp"
#include "resource/Types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace windows::animation
{
    class BoneMaskEditorPanel
    {
    public:
        void draw(animator::AnimatorData* animatorData, bool& isDirty);

    private:
        int selectedMaskIndex = -1;
        std::string newMaskName;
        std::string newBoneName;

        void drawMaskList(animator::AnimatorData* animatorData, bool& isDirty);
        void drawBoneTree(animator::BoneMaskDefinition& mask, bool& isDirty);
    };
}
