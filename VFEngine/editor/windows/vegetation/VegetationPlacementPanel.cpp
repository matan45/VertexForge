#include "VegetationPlacementPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/vegetation/VegetationEvents.hpp"
#include <imgui.h>

namespace windows
{
    VegetationPlacementPanel::~VegetationPlacementPanel()
    {
        if (subscribed)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.unsubscribe(modeToken);
        }
    }

    void VegetationPlacementPanel::subscribe()
    {
        if (subscribed) return;

        auto& dispatcher = events::EventDispatcher::instance();

        modeToken = dispatcher.subscribe<events::vegetationBrush::VegetationPlacementModeChangedNotification>(
            [this](const auto& n)
            {
                visible = n.isActive;
                if (n.isActive) needsInitialParamSend = true;
            });

        subscribed = true;
    }

    void VegetationPlacementPanel::sendBrushParams()
    {
        events::vegetationBrush::SetPlacementBrushParamsCommand cmd;
        cmd.params.radius = brushRadius;
        cmd.params.density = density;
        cmd.params.strength = strength;
        cmd.params.opacity = opacity;
        cmd.params.minScale = minScale;
        cmd.params.maxScale = maxScale;
        cmd.params.randomRotation = randomRotation;
        cmd.params.speciesId = static_cast<uint32_t>(selectedSpeciesId);
        events::EventDispatcher::instance().execute(cmd);
    }

    void VegetationPlacementPanel::draw()
    {
        if (!subscribed) subscribe();
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Vegetation Placement", &visible))
        {
            ImGui::End();
            return;
        }

        bool paramsChanged = false;

        // Brush type
        const char* brushTypes[] = {"Spread", "Erase"};
        if (ImGui::Combo("Tool", &selectedBrushType, brushTypes, IM_ARRAYSIZE(brushTypes)))
        {
            events::vegetationBrush::SetPlacementBrushTypeCommand cmd;
            cmd.type = static_cast<vegetation::PlacementBrushType>(selectedBrushType);
            events::EventDispatcher::instance().execute(cmd);
        }

        paramsChanged |= ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f);
        paramsChanged |= ImGui::SliderFloat("Density", &density, 0.0f, 1.0f);
        paramsChanged |= ImGui::SliderFloat("Strength", &strength, 0.0f, 100.0f);
        paramsChanged |= ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Scale");
        paramsChanged |= ImGui::SliderFloat("Min Scale", &minScale, 0.1f, 3.0f);
        paramsChanged |= ImGui::SliderFloat("Max Scale", &maxScale, 0.1f, 3.0f);
        paramsChanged |= ImGui::SliderFloat("Random Rotation", &randomRotation, 0.0f, 1.0f);

        // Species selector - dropdown from registered species
        ImGui::Separator();
        ImGui::Text("Species");

        auto allSpecies = events::EventDispatcher::instance().query(
            events::vegetation::GetAllVegetationSpeciesQuery{});

        if (allSpecies.empty())
        {
            ImGui::TextDisabled("No species registered");
            ImGui::TextWrapped("Add species in the Species panel first.");
        }
        else
        {
            // Build species list for combo
            speciesIds.clear();
            speciesNames.clear();
            int currentIdx = -1;

            for (const auto& [id, config] : allSpecies)
            {
                if (id == static_cast<uint32_t>(selectedSpeciesId))
                {
                    currentIdx = static_cast<int>(speciesIds.size());
                }
                speciesIds.push_back(id);
                speciesNames.push_back(config.name.empty()
                    ? ("Species " + std::to_string(id))
                    : config.name);
            }

            // If selected species no longer exists, select first
            if (currentIdx < 0 && !speciesIds.empty())
            {
                currentIdx = 0;
                selectedSpeciesId = speciesIds[0];
                paramsChanged = true;
            }

            // Send params when mode first activates to ensure correct species ID
            if (needsInitialParamSend && !speciesIds.empty())
            {
                if (currentIdx >= 0) selectedSpeciesId = speciesIds[currentIdx];
                paramsChanged = true;
                needsInitialParamSend = false;
            }

            // Ensure params are sent when mode first activates
            if (needsInitialParamSend && !speciesIds.empty())
            {
                if (currentIdx >= 0)
                    selectedSpeciesId = speciesIds[currentIdx];
                paramsChanged = true;
                needsInitialParamSend = false;
            }

            if (ImGui::BeginCombo("Active Species", currentIdx >= 0
                ? speciesNames[currentIdx].c_str() : "None"))
            {
                for (int i = 0; i < static_cast<int>(speciesIds.size()); ++i)
                {
                    bool isSelected = (i == currentIdx);
                    if (ImGui::Selectable(speciesNames[i].c_str(), isSelected))
                    {
                        currentIdx = i;
                        selectedSpeciesId = speciesIds[i];
                        paramsChanged = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (paramsChanged)
        {
            sendBrushParams();
        }

        ImGui::End();

        if (!visible)
        {
            events::vegetationBrush::SetVegetationPlacementModeActiveCommand cmd;
            cmd.active = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }
}
