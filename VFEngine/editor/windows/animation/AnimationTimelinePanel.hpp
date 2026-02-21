#pragma once

#include "resource/Types.hpp"
#include "animator/AnimationEventTypes.hpp"
#include "providers/PreviewInstanceId.hpp"
#include <memory>
#include <vector>
#include <string>

namespace ImSequencer
{
    struct SequenceInterface;
}

namespace windows::animation
{
    struct TimelineDrawContext
    {
        int& currentFrame;
        int& selectedChannel;
        bool& sequencerExpanded;
        int& firstFrame;
        const resource::AnimationData* animationData = nullptr;
        const services::PreviewInstanceId& instanceId;
        std::vector<animator::AnimationEvent>* events = nullptr;
        const std::string& animationPath;
    };

    class AnimationTimelinePanel
    {
    private:
        class AnimationSequence;
        std::unique_ptr<AnimationSequence> sequenceAdapter;

        int selectedEventIndex = -1;
        char newEventName[128] = "";
        char newEventPayload[256] = "";
        bool eventSaveSuccess = false;
        float eventSaveMessageTimer = 0.0f;

    public:
        explicit AnimationTimelinePanel();
        ~AnimationTimelinePanel();

        void setAnimationData(const resource::AnimationData* data);
        void draw(const TimelineDrawContext& ctx);

    private:
        void seekToTime(float timeInTicks, const resource::AnimationData* animationData,
                        const services::PreviewInstanceId& instanceId);
        void drawEventMarkers(const std::vector<animator::AnimationEvent>& events,
                              float duration, int firstFrame);
        void drawEventEditor(std::vector<animator::AnimationEvent>& events,
                             const std::string& animationPath);
        void drawEventSaveButton(const std::vector<animator::AnimationEvent>& events,
                                 const std::string& animationPath);
        void drawEventList(std::vector<animator::AnimationEvent>& events);
        void drawAddEventForm(std::vector<animator::AnimationEvent>& events);
        int timeToFrame(float timeInTicks) const;
        float frameToTime(int frame) const;
    };
}
