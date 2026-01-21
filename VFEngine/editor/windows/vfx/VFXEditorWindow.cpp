#include "VFXEditorWindow.hpp"
#include "VFXPreviewPanel.hpp"
#include "../../graph/VFXGraphEditor.hpp"
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXTypes.hpp>
#include <vfx/VFXModifierConfigLoader.hpp>
#include <providers/IVFXPreviewProvider.hpp>
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include <filesystem>

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

        // VK-238: Extract modifier chain from graph
        params.modifiers = vfx::VFXModifierConfigLoader::fromGraph(vfxData->graph);

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

                ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, contentSize.y), true);
                previewPanel->draw();
                ImGui::EndChild();

                if (needsPreviewUpdate)
                {
                    updatePreviewFromGraph();
                    previewPanel->play();
                    needsPreviewUpdate = false;
                }

                ImGui::SameLine();

                float graphWidth = contentSize.x - previewPanelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("GraphPanel", ImVec2(graphWidth, contentSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                drawGraphPanel();
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
}
