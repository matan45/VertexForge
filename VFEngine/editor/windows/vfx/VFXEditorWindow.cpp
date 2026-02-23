#include "VFXEditorWindow.hpp"
#include "VFXPreviewPanel.hpp"
#include "../../graph/VFXGraphEditor.hpp"
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXTypes.hpp>
#include <vfx/VFXModifierConfigLoader.hpp>
#include <vfx/VFXForceConfigLoader.hpp>
#include <vfx/VFXShapeConfigLoader.hpp>
#include <providers/IVFXPreviewProvider.hpp>
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include <filesystem>
#include <algorithm>

namespace windows
{
    VFXEditorWindow::VFXEditorWindow(const std::string& vfxPath)
        : vfxPath(vfxPath)
        , graphEditor(std::make_unique<editor::graph::VFXGraphEditor>())
        , previewPanel(std::make_unique<editor::vfxeditor::VFXPreviewPanel>(this))
    {
        std::filesystem::path path(vfxPath);
        windowTitle = "VFX Editor: " + path.filename().string();
    }

    VFXEditorWindow::~VFXEditorWindow()
    {
        if (graphEditor)
        {
            graphEditor->cleanUp();
        }
    }

    void VFXEditorWindow::initEditor()
    {
        graphEditor->init();
        loadVFX();

        if (vfxData)
        {
            graphEditor->setGraph(&vfxData->graph);
            graphEditor->setOnGraphChanged([this]() { onGraphChanged(); });
            graphEditor->navigateToContent();
            needsPreviewUpdate = true;

            propertyPanel.setOnPropertyChanged([this]() { onGraphChanged(); });
        }
    }

    void VFXEditorWindow::loadVFX()
    {
        auto loadedData = vfx::VFXAsset::load(vfxPath);

        if (loadedData.has_value())
        {
            vfxData = std::make_unique<vfx::VFXData>(std::move(loadedData.value()));
        }
        else
        {
            vfLogInfo("Creating new VFX: {}", vfxPath);
            std::filesystem::path path(vfxPath);
            vfxData = std::make_unique<vfx::VFXData>(
                vfx::VFXAsset::createDefault(path.stem().string()));
        }
    }

    void VFXEditorWindow::saveVFX()
    {
        if (!vfxData) return;

        if (vfx::VFXAsset::save(vfxPath, *vfxData))
        {
            isDirty = false;
            vfLogInfo("VFX saved: {}", vfxPath);
        }
        else
        {
            vfLogError("Failed to save VFX: {}", vfxPath);
        }
    }

    void VFXEditorWindow::onGraphChanged()
    {
        isDirty = true;
        updatePreviewFromGraph();
    }

    void VFXEditorWindow::updatePreviewFromGraph()
    {
        if (!vfxData) return;

        const vfx::VFXNode* emitterNode = vfxData->graph.findEmitterNode();
        if (!emitterNode) return;

        auto getFloat = [](const vfx::VFXNode& node, const std::string& propName, float defaultValue) -> float {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<float>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getVec3 = [](const vfx::VFXNode& node, const std::string& propName, const glm::vec3& defaultValue) -> glm::vec3 {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<glm::vec3>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getVec4 = [](const vfx::VFXNode& node, const std::string& propName, const glm::vec4& defaultValue) -> glm::vec4 {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<glm::vec4>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getBool = [](const vfx::VFXNode& node, const std::string& propName, bool defaultValue) -> bool {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<bool>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getInt = [](const vfx::VFXNode& node, const std::string& propName, int32_t defaultValue) -> int32_t {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<int32_t>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getString = [](const vfx::VFXNode& node, const std::string& propName, const std::string& defaultValue) -> std::string {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<std::string>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        services::VFXPreviewParams params;
        params.spawnRate = getFloat(*emitterNode, "spawnRate", vfx::EmitterDefaults::SPAWN_RATE);
        params.lifetime = getFloat(*emitterNode, "lifetime", vfx::EmitterDefaults::LIFETIME);
        params.startSize = getFloat(*emitterNode, "startSize", vfx::EmitterDefaults::START_SIZE);
        params.startSpeed = getFloat(*emitterNode, "startSpeed", vfx::EmitterDefaults::START_SPEED);
        params.emitDirection = getVec3(*emitterNode, "startVelocity", glm::vec3(0.0f, 1.0f, 0.0f));
        params.startColor = getVec4(*emitterNode, "startColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        params.looping = getBool(*emitterNode, "looping", vfx::EmitterDefaults::LOOPING);
        params.texturePath = getString(*emitterNode, "texture", "");

        params.modifiers = vfx::VFXModifierConfigLoader::fromGraph(vfxData->graph);
        params.forces = vfx::VFXForceConfigLoader::fromGraph(vfxData->graph);
        params.shape = vfx::VFXShapeConfigLoader::fromGraph(vfxData->graph);

        params.flipbookRows = std::clamp(getInt(*emitterNode, "flipbookRows", vfx::EmitterDefaults::FLIPBOOK_ROWS), 1, 16);
        params.flipbookColumns = std::clamp(getInt(*emitterNode, "flipbookColumns", vfx::EmitterDefaults::FLIPBOOK_COLUMNS), 1, 16);
        params.flipbookFrameRate = getFloat(*emitterNode, "flipbookFrameRate", vfx::EmitterDefaults::FLIPBOOK_FRAME_RATE);
        params.flipbookRandomStart = getBool(*emitterNode, "flipbookRandomStart", vfx::EmitterDefaults::FLIPBOOK_RANDOM_START);

        // Rendering
        params.alphaClipThreshold = getFloat(*emitterNode, "alphaClipThreshold", vfx::EmitterDefaults::ALPHA_CLIP_THRESHOLD);
        params.additiveBlend = getBool(*emitterNode, "additiveBlend", vfx::EmitterDefaults::ADDITIVE_BLEND);

        params.renderMode = getInt(*emitterNode, "renderMode", vfx::EmitterDefaults::RENDER_MODE);
        params.softParticleDistance = getFloat(*emitterNode, "softParticleDistance", vfx::EmitterDefaults::SOFT_PARTICLE_DISTANCE);
        params.stretchMultiplier = getFloat(*emitterNode, "stretchMultiplier", vfx::EmitterDefaults::STRETCH_MULTIPLIER);

        params.meshPath = getString(*emitterNode, "meshPath", "");

        params.maxTrailPoints = getInt(*emitterNode, "maxTrailPoints", vfx::EmitterDefaults::MAX_TRAIL_POINTS);
        params.ribbonWidth = getFloat(*emitterNode, "ribbonWidth", vfx::EmitterDefaults::RIBBON_WIDTH);
        params.ribbonMinDistance = getFloat(*emitterNode, "ribbonMinDistance", vfx::EmitterDefaults::RIBBON_MIN_DISTANCE);

        params.uvScrollSpeedU = getFloat(*emitterNode, "uvScrollSpeedU", vfx::EmitterDefaults::UV_SCROLL_SPEED_U);
        params.uvScrollSpeedV = getFloat(*emitterNode, "uvScrollSpeedV", vfx::EmitterDefaults::UV_SCROLL_SPEED_V);

        previewPanel->setParams(params);
    }

    void VFXEditorWindow::draw()
    {
        if (!isOpen)
        {
            previewPanel->cleanup();
            return;
        }

        if (needsInit)
        {
            initEditor();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);

        std::string title = windowTitle + (isDirty ? " *" : "  ");

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (isOpen)
            {
                drawToolbar();

                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                float spacing = ImGui::GetStyle().ItemSpacing.y;
                float topHeight = contentSize.y - propertyPanelHeight - spacing;

                // Top row: Preview | Graph
                ImGui::BeginChild("TopRow", ImVec2(0, topHeight), false, ImGuiWindowFlags_NoScrollbar);
                {
                    ImVec2 topSize = ImGui::GetContentRegionAvail();

                    ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, topSize.y), true);
                    previewPanel->draw();
                    ImGui::EndChild();

                    if (needsPreviewUpdate)
                    {
                        updatePreviewFromGraph();
                        previewPanel->play();
                        needsPreviewUpdate = false;
                    }

                    ImGui::SameLine();

                    float graphWidth = topSize.x - previewPanelWidth - ImGui::GetStyle().ItemSpacing.x;
                    ImGui::BeginChild("GraphPanel", ImVec2(graphWidth, topSize.y), true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    drawGraphPanel();
                    ImGui::EndChild();
                }
                ImGui::EndChild();

                // Bottom row: Property Panel
                ImGui::BeginChild("PropertyPanel", ImVec2(0, propertyPanelHeight), true);
                drawPropertyPanel();
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void VFXEditorWindow::drawToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveVFX();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Navigate to Content"))
                {
                    graphEditor->navigateToContent();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        if (ImGui::Button("Save"))
        {
            saveVFX();
        }

        ImGui::Separator();
    }

    void VFXEditorWindow::drawGraphPanel()
    {
        if (graphEditor && vfxData)
        {
            graphEditor->draw();
        }
        else
        {
            ImGui::TextDisabled("No VFX loaded");
        }
    }

    void VFXEditorWindow::drawPropertyPanel()
    {
        if (vfxData && graphEditor)
        {
            propertyPanel.draw(&vfxData->graph, graphEditor->getSelectedNodeId());
        }
        else
        {
            ImGui::TextDisabled("Select a modifier node to edit its curve or gradient");
        }
    }
}
