#include "print/Log.hpp"
#include "VFXSequenceDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>
#include <cstring>
#include <fstream>

namespace windows::details
{
    namespace
    {
        // Mirrors VFXDrawer's "filename only" display of an AssetRef.
        std::string assetFileName(const asset::AssetRef& ref)
        {
            std::string filename = ref.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            return filename;
        }

        // Editable text field backed by a std::string (char-buffer idiom used by IKDrawer).
        bool textField(const char* label, std::string& value)
        {
            char buffer[256];
            std::strncpy(buffer, value.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText(label, buffer, sizeof(buffer)))
            {
                value = buffer;
                return true;
            }
            return false;
        }
    }

    bool VFXSequenceDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::VFXSequenceComponent>(entity))
        {
            return false;
        }

        auto& seq = registry.get<components::VFXSequenceComponent>(entity);

        ImGui::PushID("VFXSequenceComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            ImGui::TextDisabled("VFX combo sequence (.vfVFXSequence)");
            ImGui::Spacing();

            drawSequenceFilePath(seq);
            ImGui::Spacing();
            drawSettings(seq);
            ImGui::Spacing();
            drawTriggers(seq);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            registry.remove<components::VFXSequenceComponent>(entity);
        }

        return true;
    }

    bool VFXSequenceDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##VFXSequenceHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("VFX Sequence");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveVFXSequence", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool VFXSequenceDrawer::drawSequenceFilePath(components::VFXSequenceComponent& seq)
    {
        bool changed = false;

        if (seq.sequenceRef.isValid())
        {
            ImGui::Text("Sequence: %s", assetFileName(seq.sequenceRef).c_str());
        }
        else
        {
            ImGui::TextDisabled("No sequence selected");
        }
        if (auto dropped = acceptAssetDropOnLastItem("VFXSequenceDrop", {".vfvfxsequence"}))
        {
            seq.sequenceRef = asset::AssetRef::fromPath(*dropped);
            changed = true;
        }

        if (ImGui::Button("Select Sequence##VFXSeq"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF VFX Sequence Files (*.vfVFXSequence)", L"*.vfVFXSequence"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    seq.sequenceRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
                else
                {
                    vfLogError("Selected sequence file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = !seq.sequenceRef.isValid();
        if (wasEmpty) ImGui::BeginDisabled();
        if (ImGui::Button("Clear##VFXSeq"))
        {
            seq.sequenceRef = asset::AssetRef::invalid();
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool VFXSequenceDrawer::drawSettings(components::VFXSequenceComponent& seq)
    {
        bool changed = false;

        if (ImGui::Checkbox("Auto Play##VFXSeq", &seq.autoPlay))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Automatically play the standalone sequence when entering play mode");
        }

        if (ImGui::Checkbox("Loop##VFXSeq", &seq.loop))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Loop the standalone sequence continuously");
        }

        changed |= textField("Socket##VFXSeq", seq.socketName);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Optional socket to attach the standalone sequence to");
        }

        return changed;
    }

    bool VFXSequenceDrawer::drawTriggers(components::VFXSequenceComponent& seq)
    {
        bool changed = false;

        ImGui::Text("Event Triggers:");
        ImGui::Indent(10.0f);

        int triggerToRemove = -1;
        for (int i = 0; i < static_cast<int>(seq.triggers.size()); ++i)
        {
            auto& trigger = seq.triggers[i];
            ImGui::PushID(i);

            changed |= textField("Event##Trigger", trigger.eventName);

            if (trigger.sequenceRef.isValid())
            {
                ImGui::Text("Sequence: %s", assetFileName(trigger.sequenceRef).c_str());
            }
            else
            {
                ImGui::TextDisabled("No sequence selected");
            }
            if (auto dropped = acceptAssetDropOnLastItem("TriggerSeqDrop", {".vfvfxsequence"}))
            {
                trigger.sequenceRef = asset::AssetRef::fromPath(*dropped);
                changed = true;
            }
            if (ImGui::Button("Select##TriggerSeq"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF VFX Sequence Files (*.vfVFXSequence)", L"*.vfVFXSequence"}});
                if (!path.empty())
                {
                    trigger.sequenceRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear##TriggerSeq"))
            {
                trigger.sequenceRef = asset::AssetRef::invalid();
                changed = true;
            }

            changed |= textField("Socket##Trigger", trigger.socketName);

            ImGui::SameLine();
            if (ImGui::Button("Remove##Trigger"))
            {
                triggerToRemove = i;
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        if (triggerToRemove >= 0)
        {
            seq.triggers.erase(seq.triggers.begin() + triggerToRemove);
            changed = true;
        }

        if (ImGui::Button("Add Trigger##VFXSeq"))
        {
            seq.triggers.emplace_back();
            changed = true;
        }

        ImGui::Unindent(10.0f);

        return changed;
    }
}
