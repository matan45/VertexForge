#include "VegetationSpeciesPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/VegetationEvents.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>

namespace windows
{
    VegetationSpeciesPanel::~VegetationSpeciesPanel()
    {
        if (subscribed)
        {
            events::EventDispatcher::instance().unsubscribe(modeToken);
        }
    }

    void VegetationSpeciesPanel::subscribe()
    {
        if (subscribed) return;

        auto& dispatcher = events::EventDispatcher::instance();

        modeToken = dispatcher.subscribe<events::vegetationBrush::VegetationPlacementModeChangedNotification>(
            [this](const auto& n)
            {
                visible = n.isActive;
                if (n.isActive) cacheValid = false;
            });

        subscribed = true;
    }

    void VegetationSpeciesPanel::refreshCache()
    {
        cachedSpecies = events::EventDispatcher::instance().query(
            events::vegetation::GetAllVegetationSpeciesQuery{});
        cacheValid = true;
    }

    void VegetationSpeciesPanel::draw()
    {
        if (!subscribed) subscribe();
        if (!visible) return;

        ImGui::Begin("Vegetation Species", &visible);

        if (!cacheValid) refreshCache();

        // Add new species button
        if (ImGui::Button("Add Species"))
        {
            vegetation::VegetationSpeciesConfig config;
            config.name = "New Species";
            events::vegetation::AddVegetationSpeciesCommand cmd;
            cmd.config = config;
            events::EventDispatcher::instance().execute(cmd);
            cacheValid = false;
        }

        ImGui::Separator();

        // Species list
        for (auto& [id, species] : cachedSpecies)
        {
            bool isSelected = (static_cast<int>(id) == selectedSpeciesId);
            char label[128];
            snprintf(label, sizeof(label), "[%u] %s", id, species.name.c_str());

            if (ImGui::Selectable(label, isSelected))
            {
                selectedSpeciesId = static_cast<int>(id);
            }
        }

        // Edit selected species
        if (selectedSpeciesId >= 0)
        {
            auto it = cachedSpecies.find(static_cast<uint32_t>(selectedSpeciesId));
            if (it != cachedSpecies.end())
            {
                ImGui::Separator();
                auto& config = it->second;
                bool changed = false;

                char nameBuf[256];
                strncpy(nameBuf, config.name.c_str(), sizeof(nameBuf) - 1);
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                {
                    config.name = nameBuf;
                    changed = true;
                }

                // Mesh path (contains LOD0-3 internally)
                ImGui::Text("Mesh: %s", config.meshPath.empty() ? "(none)" : config.meshPath.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Browse##Mesh"))
                {
                    nfd::FileDialog dialog;
                    std::string path = dialog.openFileDialog(
                        {{L"VF Mesh Files (*.vfMesh)", L"*.vfMesh"}});
                    if (!path.empty())
                    {
                        config.meshPath = path;
                        changed = true;
                    }
                }

                // Material path
                ImGui::Text("Material: %s", config.materialPath.empty() ? "(none)" : config.materialPath.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Browse##Mat"))
                {
                    nfd::FileDialog dialog;
                    std::string path = dialog.openFileDialog(
                        {{L"VF Material (*.vfMat, *.vfMatInstance)", L"*.vfMat;*.vfMatInstance"}});
                    if (!path.empty())
                    {
                        config.materialPath = path;
                        changed = true;
                    }
                }

                ImGui::Separator();
                ImGui::Text("Distances");
                changed |= ImGui::DragFloat("LOD1 Dist", &config.lod1Distance, 1.0f, 1.0f, 2000.0f);
                changed |= ImGui::DragFloat("LOD2 Dist", &config.lod2Distance, 1.0f, 1.0f, 2000.0f);
                changed |= ImGui::DragFloat("Max Render Dist", &config.maxRenderDistance, 1.0f, 1.0f, 5000.0f);

                ImGui::Separator();
                ImGui::Text("Properties");
                changed |= ImGui::Checkbox("Has Collision", &config.hasCollision);
                if (config.hasCollision)
                {
                    changed |= ImGui::DragFloat("Collision Radius", &config.collisionRadius, 0.01f, 0.01f, 5.0f);
                    changed |= ImGui::DragFloat("Collision Height", &config.collisionHeight, 0.1f, 0.1f, 50.0f);
                }

                if (changed)
                {
                    events::vegetation::UpdateVegetationSpeciesCommand cmd;
                    cmd.speciesId = static_cast<uint32_t>(selectedSpeciesId);
                    cmd.config = config;
                    events::EventDispatcher::instance().execute(cmd);
                }

                if (ImGui::Button("Remove Species"))
                {
                    events::vegetation::RemoveVegetationSpeciesCommand cmd;
                    cmd.speciesId = static_cast<uint32_t>(selectedSpeciesId);
                    events::EventDispatcher::instance().execute(cmd);
                    selectedSpeciesId = -1;
                    cacheValid = false;
                }
            }
        }

        ImGui::End();
    }
}
