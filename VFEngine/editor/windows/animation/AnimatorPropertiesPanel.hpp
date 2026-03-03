#pragma once

#include "animator/AnimatorTypes.hpp"
#include "animator/BlendTreeTypes.hpp"
#include <string>

namespace windows::animation
{
    class AnimatorPropertiesPanel
    {
    public:
        void drawParametersPanel(animator::AnimatorData* animatorData,
                                  bool& isDirty,
                                  bool& showAddParameterPopup);

        void drawStatePropertiesPanel(animator::AnimatorData* animatorData,
                                       uint32_t selectedStateId,
                                       bool& isDirty);

        void drawTransitionPropertiesPanel(animator::AnimatorData* animatorData,
                                            uint32_t selectedTransitionId,
                                            bool& isDirty);

        void drawAddParameterPopup(animator::AnimatorData* animatorData,
                                    bool& showAddParameterPopup,
                                    std::string& newParameterName,
                                    animator::AnimatorParameterType& newParameterType,
                                    bool& isDirty);

    private:
        void drawParameterEditor(animator::AnimatorParameter& param, size_t index, bool& isDirty);
        void drawConditionEditor(animator::TransitionCondition& condition,
                                  animator::AnimatorData* animatorData,
                                  bool& isDirty);
        void drawBlendTreeEditor(animator::AnimatorState* state,
                                  animator::AnimatorData* animatorData,
                                  bool& isDirty);
    };
}
