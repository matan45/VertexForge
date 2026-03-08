#include "print/Log.hpp"
#include "BillboardDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/BillboardEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace windows::details
{
    bool BillboardDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasBillboardComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasBillboard = dispatcher.query(hasQuery);

        if (!hasBillboard)
        {
            return false;
        }

        events::scene::GetBillboardDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("BillboardComponent");

        bool removeBillboard = false;
        bool isOpen = drawHeader(removeBillboard);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::BillboardData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Camera-facing textured quad");
            ImGui::Spacing();

            changed |= drawTexturePath(data);
            ImGui::Spacing();
            changed |= drawRenderTextureSource(data);
            ImGui::Spacing();
            changed |= drawSizeInput(data);
            ImGui::Spacing();
            changed |= drawColorTint(data);
            ImGui::Spacing();
            changed |= drawDistanceSettings(data);
            ImGui::Spacing();

            if (!data.imposterPath.empty())
            {
                std::string filename = data.imposterPath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("Impostor: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No impostor selected");
            }

            if (ImGui::Button("Select Impostor##Billboard"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Impostor Files (*.vfImposter)", L"*.vfImposter"}});
                if (!path.empty())
                {
                    std::ifstream file(path);
                    if (file.good())
                    {
                        file.close();
                        data.imposterPath = path;
                        changed = true;
                    }
                    else
                    {
                        vfLogError("Selected impostor file does not exist or cannot be read: {}", path);
                    }
                }
            }

            ImGui::SameLine();
            bool imposterEmpty = data.imposterPath.empty();
            if (imposterEmpty) ImGui::BeginDisabled();
            if (ImGui::Button("Clear##BillboardImposter"))
            {
                data.imposterPath = "";
                changed = true;
            }
            if (imposterEmpty) ImGui::EndDisabled();

            ImGui::SameLine();
            drawBakeImpostor(handle);

            if (changed)
            {
                events::scene::SetBillboardDataCommand cmd;
                cmd.entity = handle;
                cmd.billboardData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeBillboard)
        {
            events::scene::RemoveBillboardComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool BillboardDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##BillboardHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Billboard");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveBillboard", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool BillboardDrawer::drawTexturePath(services::BillboardData& data)
    {
        bool changed = false;

        if (!data.texturePath.empty())
        {
            std::string filename = data.texturePath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("Texture: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No texture selected");
        }

        if (ImGui::Button("Select Texture##Billboard"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Image Files (*.vfImage)", L"*.vfImage"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    data.texturePath = path;
                    changed = true;
                }
                else
                {
                    vfLogError("Selected texture file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = data.texturePath.empty();
        if (wasEmpty) ImGui::BeginDisabled();
        if (ImGui::Button("Clear##BillboardTex"))
        {
            data.texturePath = "";
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool BillboardDrawer::drawRenderTextureSource(services::BillboardData& data)
    {
        return rttPicker.draw("BillboardRTT", data.renderTextureSourceName, data.renderTextureSource);
    }

    bool BillboardDrawer::drawSizeInput(services::BillboardData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat2("Size##Billboard", &data.size.x, 0.01f, 0.01f, 100.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Billboard size in world units");
        }

        return changed;
    }

    bool BillboardDrawer::drawColorTint(services::BillboardData& data)
    {
        bool changed = false;

        if (ImGui::ColorEdit4("Color Tint##Billboard", &data.colorTint.x))
        {
            changed = true;
        }

        return changed;
    }

    bool BillboardDrawer::drawDistanceSettings(services::BillboardData& data)
    {
        bool changed = false;

        ImGui::SeparatorText("GPU Billboard");

        if (ImGui::DragFloat("Billboard Distance##Billboard", &data.billboardDistance, 1.0f, 1.0f, 10000.0f, "%.0f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Distance at which mesh transitions to billboard impostor");
        }

        if (ImGui::DragFloat("Max Render Distance##Billboard", &data.maxRenderDistance, 1.0f, 1.0f, 50000.0f, "%.0f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Maximum distance at which the billboard is rendered");
        }

        return changed;
    }

    void BillboardDrawer::drawBakeImpostor(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Check if entity has a mesh to bake from
        events::scene::GetMeshDataQuery meshQuery;
        meshQuery.entity = handle;
        auto meshDataOpt = dispatcher.query(meshQuery);

        bool hasMesh = meshDataOpt.has_value() && !meshDataOpt->meshPath.empty();
        if (!hasMesh) ImGui::BeginDisabled();

        if (ImGui::Button("Bake Impostor##Billboard"))
        {
            std::string meshPath = meshDataOpt->meshPath;

            // Generate output path next to mesh file
            std::string outputPath = meshPath;
            auto dotPos = outputPath.rfind('.');
            if (dotPos != std::string::npos)
            {
                outputPath = outputPath.substr(0, dotPos);
            }
            outputPath += ".vfImposter";

            // Compute mesh center and scale from bounding box + entity transform
            glm::vec3 bakeMeshCenter(0.0f);
            float bakeMeshScale = 1.0f;

            events::scene::GetWorldTransformQuery transformQuery;
            transformQuery.entity = handle;
            auto transformOpt = dispatcher.query(transformQuery);

            events::render::GetMeshBoundingBoxQuery bbQuery;
            bbQuery.meshPath = meshPath;
            auto bbOpt = dispatcher.query(bbQuery);

            if (bbOpt.has_value() && transformOpt.has_value())
            {
                auto& bb = *bbOpt;
                auto& transform = *transformOpt;

                // Mesh BB center in local space, offset by entity world position
                glm::vec3 bbCenter = (bb.min + bb.max) * 0.5f;
                bakeMeshCenter = transform.position + bbCenter * transform.scale;

                // Scale = half the max extent of the scaled bounding box
                glm::vec3 extent = (bb.max - bb.min) * 0.5f * transform.scale;
                bakeMeshScale = std::max({std::abs(extent.x), std::abs(extent.y), std::abs(extent.z)});
            }
            else if (bbOpt.has_value())
            {
                auto& bb = *bbOpt;
                bakeMeshCenter = (bb.min + bb.max) * 0.5f;
                glm::vec3 extent = (bb.max - bb.min) * 0.5f;
                bakeMeshScale = std::max({std::abs(extent.x), std::abs(extent.y), std::abs(extent.z)});
            }

            events::render::BakeImposterCommand cmd;
            cmd.meshPath = meshPath;
            cmd.outputPath = outputPath;
            cmd.meshCenter = bakeMeshCenter;
            cmd.meshScale = bakeMeshScale;
            auto result = dispatcher.execute(cmd);

            if (result.success)
            {
                vfLogInfo("Impostor baked: {}", result.outputPath);

                // Set the imposter path on the billboard component
                events::scene::GetBillboardDataQuery bbQuery;
                bbQuery.entity = handle;
                auto bbDataOpt = dispatcher.query(bbQuery);
                if (bbDataOpt.has_value())
                {
                    auto bbData = *bbDataOpt;
                    bbData.imposterPath = result.outputPath;
                    events::scene::SetBillboardDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.billboardData = bbData;
                    dispatcher.execute(setCmd);
                }
            }
            else
            {
                vfLogError("Impostor bake failed: {}", result.errorMessage);
            }
        }

        if (!hasMesh) ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (hasMesh)
            {
                ImGui::SetTooltip("Bake impostor atlas from entity mesh");
            }
            else
            {
                ImGui::SetTooltip("Entity needs a MeshComponent to bake impostor");
            }
        }
    }
}
