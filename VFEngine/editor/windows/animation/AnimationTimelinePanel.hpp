#pragma once

#include "resource/Types.hpp"
#include <memory>

namespace ImSequencer { struct SequenceInterface; }
namespace services { struct PreviewInstanceId; }

namespace windows::animation
{
    class AnimationTimelinePanel
    {
    public:
        explicit AnimationTimelinePanel();
        ~AnimationTimelinePanel();

        void setAnimationData(const resource::AnimationData* data);
        void draw(int& currentFrame, int& selectedChannel, bool& sequencerExpanded, int& firstFrame,
                  const resource::AnimationData* animationData,
                  const services::PreviewInstanceId& instanceId);

    private:
        void seekToTime(float timeInTicks, const resource::AnimationData* animationData,
                        const services::PreviewInstanceId& instanceId);
        int timeToFrame(float timeInTicks) const;
        float frameToTime(int frame) const;

        class AnimationSequence;
        std::unique_ptr<AnimationSequence> sequenceAdapter;
    };
}
