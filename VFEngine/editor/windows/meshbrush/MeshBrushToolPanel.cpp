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
                if (n.isActive)
                {
                    pushParams();
                }
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

        const char* modes[] = {"Paint", "Erase"};
        if (ImGui::Combo("Mode", &selectedMode, modes, IM_ARRAYSIZE(modes)))
        {
            events::meshBrush::SetMeshBrushModeCommand cmd;
            cmd.mode = static_cast<meshbrush::MeshBrushMode>(selectedMode);
            events::EventDispatcher::instance().execute(cmd);
        }

        if (selectedMode == static_cast<int>(meshbrush::MeshBrushMode::Erase))
        {
            if (ImGui::Checkbox("Erase Selected Type Only", &eraseSelectedTypeOnly))
                pushParams();
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

        drawPaletteCombo();

        ImGui::Separator();

        int removeIndex = -1;

        for (int i = 0; i < static_cast<int>(paletteEntries.size()); ++i)
        {
            drawPaletteEntry(i, removeIndex);
        }

        if (removeIndex >= 0)
        {
            paletteDirty = true;
            paletteEntries.erase(paletteEntries.begin() + removeIndex);

            if (selectedPaletteIndex == removeIndex)
            {
                selectedPaletteIndex = -1;
                events::meshBrush::SetMeshBrushSelectedEntryCommand selCmd;
                selCmd.selectedIndex = -1;
                events::EventDispatcher::instance().execute(selCmd);
            }
            else if (selectedPaletteIndex > removeIndex)
            {
                --selectedPaletteIndex;
                events::meshBrush::SetMeshBrushSelectedEntryCommand selCmd;
                selCmd.selectedIndex = selectedPaletteIndex;
                events::EventDispatcher::instance().execute(selCmd);
            }
        }

        if (ImGui::Button("Add Entry"))
        {
            paletteDirty = true;
            meshbrush::MeshPaletteEntry newEntry;
            paletteEntries.push_back(newEntry);
        }

        if (paletteDirty)
        {
            events::meshBrush::SetMeshBrushPaletteCommand cmd;
            cmd.palette = paletteEntries;
            events::EventDispatcher::instance().execute(cmd);
            paletteDirty = false;
        }
    }

    void MeshBrushToolPanel::drawPaletteCombo()
    {
        std::string preview = "All (Random)";
        if (selectedPaletteIndex >= 0 && selectedPaletteIndex < static_cast<int>(paletteEntries.size()))
        {
            const auto& sel = paletteEntries[selectedPaletteIndex];
            preview = "Entry " + std::to_string(selectedPaletteIndex);
            if (!sel.meshPath.empty())
            {
                auto pos = sel.meshPath.find_last_of("\\/");
                std::string filename = (pos != std::string::npos)
                    ? sel.meshPath.substr(pos + 1) : sel.meshPath;
                preview += " - " + filename;
            }
        }

        if (ImGui::BeginCombo("Paint Entry", preview.c_str()))
        {
            bool isAllSelected = (selectedPaletteIndex == -1);
            if (ImGui::Selectable("All (Random)", isAllSelected))
            {
                selectedPaletteIndex = -1;
                events::meshBrush::SetMeshBrushSelectedEntryCommand cmd;
                cmd.selectedIndex = -1;
                events::EventDispatcher::instance().execute(cmd);
            }
            if (isAllSelected) ImGui::SetItemDefaultFocus();

            for (int i = 0; i < static_cast<int>(paletteEntries.size()); ++i)
            {
                std::string itemLabel = "Entry " + std::to_string(i);
                if (!paletteEntries[i].meshPath.empty())
                {
                    auto pos = paletteEntries[i].meshPath.find_last_of("\\/");
                    std::string filename = (pos != std::string::npos)
                        ? paletteEntries[i].meshPath.substr(pos + 1) : paletteEntries[i].meshPath;
                    itemLabel += " - " + filename;
                }
                bool isSelected = (selectedPaletteIndex == i);
                if (ImGui::Selectable(itemLabel.c_str(), isSelected))
                {
                    selectedPaletteIndex = i;
                    events::meshBrush::SetMeshBrushSelectedEntryCommand cmd;
                    cmd.selectedIndex = i;
                    events::EventDispatcher::instance().execute(cmd);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }
    }

    void MeshBrushToolPanel::drawPaletteEntry(int index, int& removeIndex)
    {
        ImGui::PushID(index);
        auto& entry = paletteEntries[index];

        ImVec4 headerColor = (selectedPaletteIndex == index)
            ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f)
            : ImVec4(0.2f, 0.6f, 0.2f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
            ImVec4(headerColor.x + 0.1f, headerColor.y + 0.1f, headerColor.z + 0.1f, 1.0f));

        std::string label = "Entry " + std::to_string(index);
        if (!entry.meshPath.empty())
        {
            auto pos = entry.meshPath.find_last_of("\\/");
            std::string filename = (pos != std::string::npos)
                ? entry.meshPath.substr(pos + 1) : entry.meshPath;
            label += " - " + filename;
        }
        if (ImGui::TreeNode("Entry", "%s", label.c_str()))
        {
            bool changed = false;

            char meshBuf[256] = {};
            strncpy(meshBuf, entry.meshPath.c_str(), sizeof(meshBuf) - 1);
            ImGui::InputText("Mesh Path", meshBuf, sizeof(meshBuf), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button("Browse##mesh"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> filters = {
                    {L"Mesh Files", L"*.vfMesh"}
                };
                std::string selectedPath = fileDialog.openFileDialog(filters);
                if (!selectedPath.empty())
                {
                    entry.meshPath = selectedPath;
                    changed = true;
                }
            }

            char matBuf[256] = {};
            strncpy(matBuf, entry.materialPath.c_str(), sizeof(matBuf) - 1);
            ImGui::InputText("Material Path", matBuf, sizeof(matBuf), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button("Browse##mat"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> filters = {
                    {L"Material Files", L"*.vfMat;*.vfMatInstance"}
                };
                std::string selectedPath = fileDialog.openFileDialog(filters);
                if (!selectedPath.empty())
                {
                    entry.materialPath = selectedPath;
                    changed = true;
                }
            }

            changed |= ImGui::DragFloat("Weight", &entry.weight, 0.1f, 0.01f, 100.0f);
            changed |= ImGui::DragFloat2("Scale Range", &entry.scaleRange.x, 0.01f, 0.01f, 10.0f);
            changed |= ImGui::DragFloat2("Rotation Y Range", &entry.rotationYRange.x, 1.0f, 0.0f, 360.0f);
            changed |= ImGui::Checkbox("Random Rotation X", &entry.randomRotationX);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Random Rotation Z", &entry.randomRotationZ);
            changed |= ImGui::Checkbox("Align To Normal", &entry.alignToNormal);
            changed |= ImGui::DragFloat("Max Slope", &entry.maxSlope, 1.0f, 0.0f, 90.0f, "%.0f deg");
            changed |= ImGui::DragFloat("Y Offset", &entry.yOffset, 0.1f, -100.0f, 100.0f);
            changed |= ImGui::Checkbox("Use Collider", &entry.useCollider);

            if (changed) paletteDirty = true;

            if (ImGui::Button("Remove"))
            {
                removeIndex = index;
            }

            ImGui::TreePop();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopID();
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
        params.eraseSelectedTypeOnly = eraseSelectedTypeOnly;

        events::meshBrush::SetMeshBrushParamsCommand cmd;
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
    }
}
