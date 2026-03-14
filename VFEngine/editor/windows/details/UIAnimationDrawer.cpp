#include "UIAnimationDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UIAnimationDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIAnimationComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasAnimation = dispatcher.query(hasQuery);

        if (!hasAnimation)
        {
            return false;
        }

        events::ui::GetUIAnimationDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIAnimationComponent");

        bool removeAnim = false;
        bool isOpen = drawHeader(removeAnim);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIAnimationData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Tween animation system for UI elements");
            ImGui::Spacing();

            // AutoPlay checkbox
            if (ImGui::Checkbox("Auto Play##UIAnim", &data.autoPlay))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Automatically play animation when the scene starts");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Playback controls
            {
                events::ui::IsUIAnimationPlayingQuery playingQuery;
                playingQuery.entity = handle;
                bool isPlaying = dispatcher.query(playingQuery);

                if (isPlaying)
                {
                    if (ImGui::Button("Stop##UIAnim"))
                    {
                        events::ui::StopUIAnimationCommand cmd;
                        cmd.entity = handle;
                        dispatcher.execute(cmd);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Pause##UIAnim"))
                    {
                        events::ui::PauseUIAnimationCommand cmd;
                        cmd.entity = handle;
                        dispatcher.execute(cmd);
                    }
                }
                else
                {
                    if (ImGui::Button("Play##UIAnim"))
                    {
                        events::ui::PlayUIAnimationCommand cmd;
                        cmd.entity = handle;
                        dispatcher.execute(cmd);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Resume##UIAnim"))
                    {
                        events::ui::ResumeUIAnimationCommand cmd;
                        cmd.entity = handle;
                        dispatcher.execute(cmd);
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Animation tree
            ImGui::Text("Animation Tree");
            ImGui::Spacing();

            int nodeId = 0;
            changed |= drawNode(data.rootNode, 0, nodeId);

            if (changed)
            {
                events::ui::SetUIAnimationDataCommand cmd;
                cmd.entity = handle;
                cmd.animationData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeAnim)
        {
            events::ui::RemoveUIAnimationComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIAnimationDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIAnimationHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Animation");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIAnimation", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIAnimationDrawer::drawNode(services::UIAnimationNodeData& node, int depth, int& nodeId)
    {
        bool changed = false;
        int currentNodeId = nodeId++;

        ImGui::PushID(currentNodeId);

        const char* nodeTypeNames[] = {"Clip", "Parallel", "Sequence"};
        int currentType = static_cast<int>(node.type);

        // Node type combo
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Node Type", &currentType, nodeTypeNames, 3))
        {
            node.type = static_cast<uint8_t>(currentType);
            changed = true;
        }

        if (node.type == 0) // Clip
        {
            changed |= drawClipEditor(node.clip, currentNodeId);
        }
        else // Parallel or Sequence
        {
            // Loop mode for group nodes
            const char* loopModeNames[] = {"Once", "Loop", "PingPong"};
            int currentLoop = static_cast<int>(node.loopMode);
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("Loop Mode##Node", &currentLoop, loopModeNames, 3))
            {
                node.loopMode = static_cast<uint8_t>(currentLoop);
                changed = true;
            }

            // Children
            ImGui::Indent(15.0f);
            for (size_t i = 0; i < node.children.size(); i++)
            {
                ImGui::PushID(static_cast<int>(i));

                ImGui::Separator();
                ImGui::Text("Child %d", static_cast<int>(i));

                ImGui::SameLine();
                if (ImGui::SmallButton("Remove##Child"))
                {
                    node.children.erase(node.children.begin() + static_cast<ptrdiff_t>(i));
                    changed = true;
                    ImGui::PopID();
                    break;
                }

                changed |= drawNode(node.children[i], depth + 1, nodeId);
                ImGui::PopID();
            }

            if (ImGui::Button("+ Add Child"))
            {
                services::UIAnimationNodeData child;
                child.type = 0; // Clip
                node.children.push_back(child);
                changed = true;
            }
            ImGui::Unindent(15.0f);
        }

        ImGui::PopID();
        return changed;
    }

    bool UIAnimationDrawer::drawClipEditor(services::UIAnimationClipData& clip, int nodeId)
    {
        bool changed = false;

        ImGui::PushID(nodeId + 10000);

        const char* propertyNames[] = {
            "Opacity", "PositionX", "PositionY", "ScaleX", "ScaleY",
            "Rotation", "ColorR", "ColorG", "ColorB", "ColorA"
        };
        int currentProp = static_cast<int>(clip.property);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Property", &currentProp, propertyNames, 10))
        {
            clip.property = static_cast<uint8_t>(currentProp);
            changed = true;
        }

        if (ImGui::DragFloat("Start Value", &clip.startValue, 0.01f))
        {
            changed = true;
        }

        if (ImGui::DragFloat("End Value", &clip.endValue, 0.01f))
        {
            changed = true;
        }

        if (ImGui::DragFloat("Duration", &clip.duration, 0.01f, 0.01f, 100.0f))
        {
            changed = true;
        }

        if (ImGui::DragFloat("Delay", &clip.delay, 0.01f, 0.0f, 100.0f))
        {
            changed = true;
        }

        const char* easingNames[] = {
            "Linear", "EaseIn", "EaseOut", "EaseInOut", "Bounce", "Elastic"
        };
        int currentEasing = static_cast<int>(clip.easing);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Easing", &currentEasing, easingNames, 6))
        {
            clip.easing = static_cast<uint8_t>(currentEasing);
            changed = true;
        }

        const char* loopModeNames[] = {"Once", "Loop", "PingPong"};
        int currentLoop = static_cast<int>(clip.loopMode);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Loop Mode##Clip", &currentLoop, loopModeNames, 3))
        {
            clip.loopMode = static_cast<uint8_t>(currentLoop);
            changed = true;
        }

        ImGui::PopID();
        return changed;
    }
}
