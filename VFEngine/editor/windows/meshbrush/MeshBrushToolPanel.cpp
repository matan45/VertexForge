#include "MeshBrushToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"
#include <imgui.h>
#include <cstring>

namespace windows
{
    MeshBrushToolPanel::~MeshBrushToolPanel()
    {
        if (subscribed)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.unsubscribe(modeToken);
        }
    }

    void MeshBrushToolPanel::subscribe()
    {
        if (subscribed) return;

        auto& dispatcher = events::EventDispatcher::instance();

        modeToken = dispatcher.subscribe<events::meshBrush::MeshBrushModeChangedNotification>(
            [this](const auto& n)
            {
                visible = n.isActive;
            });

        subscribed = true;
    }

    void MeshBrushToolPanel::draw()
    {
        if (!subscribed) subscribe();
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Mesh Brush Tool", &visible))
        {
            ImGui::End();
            return;
        }

        // Paint/Erase mode toggle
        const char* modes[] = {"Paint", "Erase"};
        if (ImGui::Combo("Mode", &selectedMode, modes, IM_ARRAYSIZE(modes)))
        {
            events::meshBrush::SetMeshBrushModeCommand cmd;
            cmd.mode = static_cast<meshbrush::MeshBrushMode>(selectedMode);
            events::EventDispatcher::instance().execute(cmd);
        }

        ImGui::Separator();
        drawBrushSettings();

        ImGui::Separator();
        drawPaletteSection();

        ImGui::End();

        if (!visible)
        {
            events::meshBrush::SetMeshBrushModeActiveCommand cmd;
            cmd.active = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void MeshBrushToolPanel::drawBrushSettings()
    {
        if (!ImGui::CollapsingHeader("Brush Settings", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        bool changed = false;

        changed |= ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f);
        changed |= ImGui::SliderFloat("Density", &brushDensity, 0.01f, 10.0f);
        changed |= ImGui::SliderFloat("Spacing", &brushSpacing, 0.1f, 50.0f);
        changed |= ImGui::Checkbox("Continuous", &continuousMode);
        changed |= ImGui::SliderFloat("Position Jitter", &positionJitter, 0.0f, 1.0f);

        const char* falloffTypes[] = {"Constant", "Linear", "Smooth", "Sharp"};
        changed |= ImGui::Combo("Falloff", &falloffIndex, falloffTypes, IM_ARRAYSIZE(falloffTypes));

        if (changed)
        {
            pushParams();
        }
    }

    void MeshBrushToolPanel::drawPaletteSection()
    {
        if (!ImGui::CollapsingHeader("Mesh Palette", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        int removeIndex = -1;

        for (int i = 0; i < static_cast<int>(paletteEntries.size()); ++i)
        {
            ImGui::PushID(i);
            auto& entry = paletteEntries[i];

            if (ImGui::TreeNode("Entry", "Entry %d", i))
            {
                // Mesh path input
                char meshBuf[256] = {};
                strncpy(meshBuf, entry.meshPath.c_str(), sizeof(meshBuf) - 1);
                if (ImGui::InputText("Mesh Path", meshBuf, sizeof(meshBuf)))
                {
                    entry.meshPath = meshBuf;
                }

                // Material path input
                char matBuf[256] = {};
                strncpy(matBuf, entry.materialPath.c_str(), sizeof(matBuf) - 1);
                if (ImGui::InputText("Material Path", matBuf, sizeof(matBuf)))
                {
                    entry.materialPath = matBuf;
                }

                ImGui::DragFloat("Weight", &entry.weight, 0.1f, 0.01f, 100.0f);
                ImGui::DragFloat2("Scale Range", &entry.scaleRange.x, 0.01f, 0.01f, 10.0f);
                ImGui::DragFloat2("Rotation Y Range", &entry.rotationYRange.x, 1.0f, 0.0f, 360.0f);
                ImGui::Checkbox("Random Rotation X", &entry.randomRotationX);
                ImGui::SameLine();
                ImGui::Checkbox("Random Rotation Z", &entry.randomRotationZ);
                ImGui::Checkbox("Align To Normal", &entry.alignToNormal);
                ImGui::DragFloat("Max Slope", &entry.maxSlope, 1.0f, 0.0f, 90.0f, "%.0f deg");
                ImGui::DragFloat2("Height Range", &entry.heightRange.x, 1.0f, -10000.0f, 10000.0f);

                if (ImGui::Button("Remove"))
                {
                    removeIndex = i;
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (removeIndex >= 0)
        {
            paletteEntries.erase(paletteEntries.begin() + removeIndex);

            events::meshBrush::RemoveMeshPaletteEntryCommand cmd;
            cmd.index = static_cast<uint32_t>(removeIndex);
            events::EventDispatcher::instance().execute(cmd);
        }

        if (ImGui::Button("Add Entry"))
        {
            meshbrush::MeshPaletteEntry newEntry;
            paletteEntries.push_back(newEntry);

            events::meshBrush::AddMeshPaletteEntryCommand cmd;
            cmd.entry = newEntry;
            events::EventDispatcher::instance().execute(cmd);
        }

        // Sync palette to service whenever entries change
        if (!paletteEntries.empty())
        {
            events::meshBrush::SetMeshBrushPaletteCommand cmd;
            cmd.palette = paletteEntries;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void MeshBrushToolPanel::pushParams()
    {
        meshbrush::MeshBrushParams params;
        params.radius = brushRadius;
        params.density = brushDensity;
        params.spacing = brushSpacing;
        params.continuousMode = continuousMode;
        params.positionJitter = positionJitter;
        params.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);

        events::meshBrush::SetMeshBrushParamsCommand cmd;
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
    }
}
