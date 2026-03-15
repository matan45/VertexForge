#include "ReverbZoneDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ReverbZoneEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool ReverbZoneDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasReverbZoneComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        events::scene::GetReverbZoneDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("ReverbZoneComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::ReverbZoneData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Use for: environmental reverb effects");
            ImGui::Spacing();

            changed |= drawShapeSettings(data);
            ImGui::Spacing();
            changed |= drawPresetSettings(data);
            ImGui::Spacing();
            changed |= drawZoneBehavior(data);
            ImGui::Spacing();
            changed |= drawDebugSettings(data);

            if (changed)
            {
                events::scene::SetReverbZoneDataCommand cmd;
                cmd.entity = handle;
                cmd.data = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::scene::RemoveReverbZoneComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool ReverbZoneDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##ReverbZoneHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Reverb Zone");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveReverbZone", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool ReverbZoneDrawer::drawShapeSettings(services::ReverbZoneData& data)
    {
        bool changed = false;

        ImGui::Text("Shape:");
        ImGui::Indent(10.0f);

        const char* shapeNames[] = {"Sphere", "Box"};
        int currentShape = static_cast<int>(data.shape);
        if (ImGui::Combo("Shape##RZ", &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames)))
        {
            data.shape = static_cast<uint8_t>(currentShape);
            changed = true;
        }

        if (data.shape == 0)
        {
            if (ImGui::SliderFloat("Radius##RZ", &data.radius, 0.1f, 500.0f, "%.1f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Radius of the spherical reverb zone");
            }
        }
        else
        {
            if (ImGui::DragFloat3("Half Extents##RZ", &data.halfExtents.x, 0.1f, 0.1f, 500.0f, "%.1f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Half-size of the box reverb zone in each axis");
            }
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool ReverbZoneDrawer::drawPresetSettings(services::ReverbZoneData& data)
    {
        bool changed = false;

        ImGui::Text("Preset:");
        ImGui::Indent(10.0f);

        // Full list of all 113 EFX reverb presets + Custom
        static const char* presetNames[] = {
            "Generic", "Padded Cell", "Room", "Bathroom", "Living Room",
            "Stone Room", "Auditorium", "Concert Hall", "Cave", "Arena",
            "Hangar", "Carpeted Hallway", "Hallway", "Stone Corridor", "Alley",
            "Forest", "City", "Mountains", "Quarry", "Plain",
            "Parking Lot", "Sewer Pipe", "Underwater", "Drugged", "Dizzy",
            "Psychotic",
            "Castle Small Room", "Castle Short Passage", "Castle Medium Room",
            "Castle Large Room", "Castle Long Passage", "Castle Hall",
            "Castle Cupboard", "Castle Courtyard", "Castle Alcove",
            "Factory Small Room", "Factory Short Passage", "Factory Medium Room",
            "Factory Large Room", "Factory Long Passage", "Factory Hall",
            "Factory Cupboard", "Factory Courtyard", "Factory Alcove",
            "Ice Palace Small Room", "Ice Palace Short Passage", "Ice Palace Medium Room",
            "Ice Palace Large Room", "Ice Palace Long Passage", "Ice Palace Hall",
            "Ice Palace Cupboard", "Ice Palace Courtyard", "Ice Palace Alcove",
            "Space Station Small Room", "Space Station Short Passage", "Space Station Medium Room",
            "Space Station Large Room", "Space Station Long Passage", "Space Station Hall",
            "Space Station Cupboard", "Space Station Alcove",
            "Wooden Small Room", "Wooden Short Passage", "Wooden Medium Room",
            "Wooden Large Room", "Wooden Long Passage", "Wooden Hall",
            "Wooden Cupboard", "Wooden Courtyard", "Wooden Alcove",
            "Sport Empty Stadium", "Sport Squash Court", "Sport Small Swimming Pool",
            "Sport Large Swimming Pool", "Sport Gymnasium", "Sport Full Stadium",
            "Sport Stadium Tannoy",
            "Prefab Workshop", "Prefab School Room", "Prefab Practise Room",
            "Prefab Outhouse", "Prefab Caravan",
            "Dome Tomb", "Pipe Small", "Dome Saint Pauls",
            "Pipe Long Thin", "Pipe Large", "Pipe Resonant",
            "Outdoors Backyard", "Outdoors Rolling Plains", "Outdoors Deep Canyon",
            "Outdoors Creek", "Outdoors Valley",
            "Mood Heaven", "Mood Hell", "Mood Memory",
            "Driving Commentator", "Driving Pit Garage",
            "Driving In-Car Racer", "Driving In-Car Sports", "Driving In-Car Luxury",
            "Driving Full Grand Prix", "Driving Empty Grand Prix", "Driving Tunnel",
            "City Streets", "City Subway", "City Museum",
            "City Library", "City Underpass", "City Abandoned",
            "Dusty Room", "Chapel", "Small Water Room",
            "Custom"
        };
        constexpr int presetCount = IM_ARRAYSIZE(presetNames);

        int currentPreset = presetCount - 1; // Default to "Custom"
        for (int i = 0; i < presetCount - 1; ++i)
        {
            if (data.presetName == presetNames[i])
            {
                currentPreset = i;
                break;
            }
        }

        // Use BeginCombo for scrollable list instead of Combo
        const char* preview = presetNames[currentPreset];
        if (ImGui::BeginCombo("Preset##RZ", preview))
        {
            for (int i = 0; i < presetCount; ++i)
            {
                bool isSelected = (currentPreset == i);
                if (ImGui::Selectable(presetNames[i], isSelected))
                {
                    currentPreset = i;
                    if (currentPreset == presetCount - 1)
                    {
                        data.presetName = "";
                    }
                    else
                    {
                        data.presetName = presetNames[currentPreset];
                    }
                    changed = true;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool ReverbZoneDrawer::drawZoneBehavior(services::ReverbZoneData& data)
    {
        bool changed = false;

        ImGui::Text("Zone Behavior:");
        ImGui::Indent(10.0f);

        if (ImGui::DragInt("Priority##RZ", &data.priority, 1.0f, -100, 100))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Higher priority zones override lower ones when overlapping");
        }

        if (ImGui::DragFloat("Falloff Distance##RZ", &data.falloffDistance, 0.1f, 0.0f, 50.0f, "%.1f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Distance over which reverb fades in at zone boundary");
        }

        if (ImGui::SliderFloat("Wet Level##RZ", &data.wetLevel, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Amount of reverb effect applied (0 = dry, 1 = full reverb)");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool ReverbZoneDrawer::drawDebugSettings(services::ReverbZoneData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Show Debug Volume##RZ", &data.showDebugVolume))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Draw wireframe volume visualization in editor");
        }

        return changed;
    }
}
