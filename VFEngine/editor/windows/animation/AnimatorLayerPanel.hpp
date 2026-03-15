#pragma once

#include "animator/AnimatorTypes.hpp"
#include <cstdint>

namespace windows::animation
{
    class AnimatorLayerPanel
    {
    public:
        void draw(animator::AnimatorData* animatorData, uint32_t& selectedLayerIndex, bool& isDirty);

    private:
        void drawLayerEntry(animator::AnimationLayerData& layer, uint32_t index,
                            uint32_t& selectedLayerIndex, bool& isDirty);
    };
}
