#include "AnimationTimelinePanel.hpp"
#include "AnimationEventWriter.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AnimationPreviewEvents.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "imgui.h"
#include "ImSequencer.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstring>

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
                                       const services::PreviewInstanceId& instanceId,
                                       std::vector<animator::AnimationEvent>* events,
                                       const std::string& animationPath)
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

        // Draw event markers on the timeline
        if (events && !events->empty())
        {
            drawEventMarkers(*events, animationData->duration, firstFrame);
        }

        // Draw event editor section
        if (events)
        {
            ImGui::Spacing();
            drawEventEditor(*events, animationData->duration, animationPath);
        }
    }

    void AnimationTimelinePanel::drawEventMarkers(const std::vector<animator::AnimationEvent>& events,
                                                    float duration, int firstFrame)
    {
        if (duration <= 0.0f) return;

        // Get the sequencer region rect from the last item
        ImVec2 seqMin = ImGui::GetItemRectMin();
        ImVec2 seqMax = ImGui::GetItemRectMax();
        float seqWidth = seqMax.x - seqMin.x;

        if (seqWidth <= 0.0f) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 eventColor = IM_COL32(255, 165, 0, 255);       // Orange
        ImU32 eventSelectedColor = IM_COL32(255, 255, 0, 255); // Yellow

        int frameMax = static_cast<int>(duration);

        for (int i = 0; i < static_cast<int>(events.size()); ++i)
        {
            const auto& event = events[i];
            float eventFrame = event.normalizedTime * duration;
            float relativeFrame = eventFrame - static_cast<float>(firstFrame);
            float normalizedX = relativeFrame / static_cast<float>(std::max(1, frameMax - firstFrame));

            if (normalizedX < 0.0f || normalizedX > 1.0f) continue;

            float x = seqMin.x + normalizedX * seqWidth;
            float y = seqMin.y;

            ImU32 color = (i == selectedEventIndex) ? eventSelectedColor : eventColor;

            // Draw triangle marker at top
            float triSize = 6.0f;
            drawList->AddTriangleFilled(
                ImVec2(x, y),
                ImVec2(x - triSize, y - triSize * 1.5f),
                ImVec2(x + triSize, y - triSize * 1.5f),
                color);

            // Draw vertical line
            drawList->AddLine(ImVec2(x, y), ImVec2(x, seqMax.y), color, 1.0f);
        }
    }

    void AnimationTimelinePanel::drawEventEditor(std::vector<animator::AnimationEvent>& events,
                                                   float duration,
                                                   const std::string& animationPath)
    {
        if (ImGui::CollapsingHeader("Animation Events"))
        {
            ImGui::Indent(10.0f);

            // Save/Load buttons
            if (!animationPath.empty())
            {
                // Decrease save message timer
                if (eventSaveMessageTimer > 0.0f)
                {
                    eventSaveMessageTimer -= ImGui::GetIO().DeltaTime;
                }

                bool canSave = !events.empty();
                if (!canSave) ImGui::BeginDisabled();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.65f, 0.2f, 1.0f));
                if (ImGui::Button("Save Events"))
                {
                    eventSaveSuccess = types::AnimationEventWriter::saveEventsToAnimation(animationPath, events);
                    eventSaveMessageTimer = 3.0f;
                }
                ImGui::PopStyleColor(2);
                if (!canSave) ImGui::EndDisabled();

                if (eventSaveMessageTimer > 0.0f)
                {
                    if (eventSaveSuccess)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Saved!");
                    }
                    else
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Save failed!");
                    }
                }

                ImGui::Separator();
            }

            // Event list
            if (!events.empty())
            {
                ImGui::Text("Events (%zu)", events.size());
                ImGui::Separator();

                int removeIdx = -1;
                for (int i = 0; i < static_cast<int>(events.size()); ++i)
                {
                    auto& event = events[i];
                    ImGui::PushID(i);

                    bool isSelected = (i == selectedEventIndex);
                    if (ImGui::Selectable(event.name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        selectedEventIndex = i;
                    }

                    if (isSelected)
                    {
                        ImGui::Indent(10.0f);

                        // Name
                        char nameBuf[128];
                        std::strncpy(nameBuf, event.name.c_str(), sizeof(nameBuf));
                        nameBuf[sizeof(nameBuf) - 1] = '\0';
                        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                        {
                            event.name = nameBuf;
                        }

                        // Normalized time
                        float time = event.normalizedTime;
                        if (ImGui::SliderFloat("Time", &time, 0.0f, 1.0f, "%.3f"))
                        {
                            event.normalizedTime = glm::clamp(time, 0.0f, 1.0f);
                        }

                        // Payload
                        char payloadBuf[256];
                        std::strncpy(payloadBuf, event.payload.c_str(), sizeof(payloadBuf));
                        payloadBuf[sizeof(payloadBuf) - 1] = '\0';
                        if (ImGui::InputText("Payload", payloadBuf, sizeof(payloadBuf)))
                        {
                            event.payload = payloadBuf;
                        }

                        if (ImGui::Button("Delete"))
                        {
                            removeIdx = i;
                        }

                        ImGui::Unindent(10.0f);
                    }

                    ImGui::PopID();
                }

                if (removeIdx >= 0)
                {
                    events.erase(events.begin() + removeIdx);
                    selectedEventIndex = -1;
                }
            }
            else
            {
                ImGui::TextDisabled("No events defined");
            }

            // Add new event
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Add Event");

            ImGui::InputText("Event Name", newEventName, sizeof(newEventName));
            ImGui::InputText("Payload##New", newEventPayload, sizeof(newEventPayload));

            bool canAdd = std::strlen(newEventName) > 0;
            if (!canAdd) ImGui::BeginDisabled();
            if (ImGui::Button("Add Event"))
            {
                animator::AnimationEvent newEvent;
                newEvent.name = newEventName;
                newEvent.normalizedTime = 0.5f;
                newEvent.payload = newEventPayload;
                events.push_back(newEvent);

                selectedEventIndex = static_cast<int>(events.size()) - 1;
                newEventName[0] = '\0';
                newEventPayload[0] = '\0';
            }
            if (!canAdd) ImGui::EndDisabled();

            ImGui::Unindent(10.0f);
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
