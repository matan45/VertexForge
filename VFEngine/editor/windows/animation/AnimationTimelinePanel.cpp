#include "AnimationTimelinePanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AnimationPreviewEvents.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "imgui.h"
#include "ImSequencer.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace windows::animation
{
    class AnimationTimelinePanel::AnimationSequence : public ImSequencer::SequenceInterface
    {
    private:
        const resource::AnimationData* animData = nullptr;
        std::vector<int> channelStarts;
        std::vector<int> channelEnds;

    public:
        void setAnimationData(const resource::AnimationData* data)
        {
            animData = data;
            channelStarts.clear();
            channelEnds.clear();

            if (!data) return;

            for (const auto& channel : data->channels)
            {
                float minTime = 0.0f;
                float maxTime = 0.0f;

                for (const auto& key : channel.positionKeys)
                {
                    maxTime = std::max(maxTime, key.time);
                }
                for (const auto& key : channel.rotationKeys)
                {
                    maxTime = std::max(maxTime, key.time);
                }
                for (const auto& key : channel.scalingKeys)
                {
                    maxTime = std::max(maxTime, key.time);
                }

                channelStarts.push_back(static_cast<int>(minTime));
                channelEnds.push_back(static_cast<int>(maxTime));
            }
        }

        int GetFrameMin() const override { return 0; }

        int GetFrameMax() const override
        {
            return animData ? static_cast<int>(animData->duration) : 0;
        }

        int GetItemCount() const override
        {
            return animData ? static_cast<int>(animData->channels.size()) : 0;
        }

        const char* GetItemLabel(int index) const override
        {
            if (!animData || index < 0 || index >= static_cast<int>(animData->channels.size()))
                return "";
            return animData->channels[index].boneName.c_str();
        }

        void Get(int index, int** start, int** end, int* type, unsigned int* color) override
        {
            if (!animData || index < 0 || index >= static_cast<int>(channelStarts.size()))
                return;

            if (start) *start = &channelStarts[index];
            if (end) *end = &channelEnds[index];
            if (type) *type = 0;
            if (color) *color = 0xFF8080AA;
        }
    };

    AnimationTimelinePanel::AnimationTimelinePanel()
        : sequenceAdapter(std::make_unique<AnimationSequence>())
    {
    }

    AnimationTimelinePanel::~AnimationTimelinePanel() = default;

    void AnimationTimelinePanel::setAnimationData(const resource::AnimationData* data)
    {
        sequenceAdapter->setAnimationData(data);
    }

    void AnimationTimelinePanel::draw(int& currentFrame, int& selectedChannel, bool& sequencerExpanded,
                                       int& firstFrame, const resource::AnimationData* animationData,
                                       const services::PreviewInstanceId& instanceId)
    {
        ImGui::Text("Timeline");
        ImGui::Separator();

        if (!animationData)
        {
            ImGui::TextDisabled("No animation loaded");
            return;
        }

        int sequenceOptions = ImSequencer::SEQUENCER_CHANGE_FRAME;

        if (ImSequencer::Sequencer(sequenceAdapter.get(), &currentFrame, &sequencerExpanded,
                                   &selectedChannel, &firstFrame, sequenceOptions))
        {
            seekToTime(frameToTime(currentFrame), animationData, instanceId);
        }
    }

    void AnimationTimelinePanel::seekToTime(float timeInTicks, const resource::AnimationData* animationData,
                                             const services::PreviewInstanceId& instanceId)
    {
        float clampedTime = glm::clamp(timeInTicks, 0.0f, animationData->duration);

        float ticksPerSec = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        float timeSeconds = clampedTime / ticksPerSec;

        services::events::animpreview::SetAnimationPlaybackTimeCommand timeCmd;
        timeCmd.instanceId = instanceId;
        timeCmd.timeSeconds = timeSeconds;
        events::EventDispatcher::instance().execute(timeCmd);
    }

    int AnimationTimelinePanel::timeToFrame(float timeInTicks) const
    {
        return static_cast<int>(timeInTicks);
    }

    float AnimationTimelinePanel::frameToTime(int frame) const
    {
        return static_cast<float>(frame);
    }
}
