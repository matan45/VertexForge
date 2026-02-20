#pragma once

#include "resource/Types.hpp"
#include "animator/AnimationEventTypes.hpp"
#include <memory>
#include <vector>

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
                  const services::PreviewInstanceId& instanceId,
                  std::vector<animator::AnimationEvent>* events = nullptr,
                  const std::string& animationPath = "");

    private:
        void seekToTime(float timeInTicks, const resource::AnimationData* animationData,
                        const services::PreviewInstanceId& instanceId);
        void drawEventMarkers(const std::vector<animator::AnimationEvent>& events,
                              float duration, int firstFrame);
        void drawEventEditor(std::vector<animator::AnimationEvent>& events, float duration,
                             const std::string& animationPath);
        int timeToFrame(float timeInTicks) const;
        float frameToTime(int frame) const;

        class AnimationSequence;
        std::unique_ptr<AnimationSequence> sequenceAdapter;

        int selectedEventIndex = -1;
        char newEventName[128] = "";
        char newEventPayload[256] = "";
        bool eventSaveSuccess = false;
        float eventSaveMessageTimer = 0.0f;
    };
}
