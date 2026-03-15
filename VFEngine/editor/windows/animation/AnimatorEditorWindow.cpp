#include "print/Log.hpp"
#include "AnimatorEditorWindow.hpp"
#include "animator/AnimatorAsset.hpp"
#include "imgui.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    AnimatorEditorWindow::AnimatorEditorWindow(const std::string& path)
        : animatorPath(path)
          , instanceId(services::PreviewInstanceId(this))
    {
        fs::path filePath(path);
        windowTitle = "Animator Editor - " + filePath.stem().string();
    }

    AnimatorEditorWindow::~AnimatorEditorWindow()
    {
        nodeGraph.cleanUp();
    }

    void AnimatorEditorWindow::initEditor()
    {
        nodeGraph.init();
        loadAnimator();
        needsPositionInit = true;
        needsNavigateToContent = true;
        needsInit = false;
    }

    void AnimatorEditorWindow::loadAnimator()
    {
        auto result = animator::AnimatorAsset::load(animatorPath);
        if (result)
        {
            animatorData = std::make_unique<animator::AnimatorData>(std::move(*result));
            isDirty = false;
            vfLogInfo("Loaded animator: {}", animatorPath);
        }
        else
        {
            vfLogError("Failed to load animator: {}", animatorPath);
            animatorData = std::make_unique<animator::AnimatorData>(
                animator::AnimatorAsset::createDefault("New Animator"));
        }
    }

    void AnimatorEditorWindow::saveAnimator()
    {
        if (!animatorData)
            return;

        if (animator::AnimatorAsset::save(animatorPath, *animatorData))
        {
            isDirty = false;
            vfLogInfo("Saved animator: {}", animatorPath);
        }
        else
        {
            vfLogError("Failed to save animator: {}", animatorPath);
        }
    }

    animator::AnimatorGraph* AnimatorEditorWindow::getActiveGraph()
    {
        if (!animatorData)
            return nullptr;

        if (!animatorData->layers.empty() && selectedLayerIndex < animatorData->layers.size())
        {
            return &animatorData->layers[selectedLayerIndex].graph;
        }

        return &animatorData->graph;
    }

    void AnimatorEditorWindow::ensureLayersInitialized()
    {
        if (!animatorData || !animatorData->layers.empty())
            return;

        // Migrate single-graph format to layers format for editing
        animator::AnimationLayerData baseLayer;
        baseLayer.name = "Base Layer";
        baseLayer.weight = 1.0f;
        baseLayer.blendMode = animator::LayerBlendMode::Override;
        baseLayer.sourceMode = animator::LayerSourceMode::StateMachine;
        baseLayer.graph = animatorData->graph;
        animatorData->layers.push_back(std::move(baseLayer));
        selectedLayerIndex = 0;
    }

    void AnimatorEditorWindow::draw()
    {
        if (needsInit)
        {
            initEditor();
            ensureLayersInitialized();
        }

        std::string title = windowTitle + (isDirty ? " *###AnimatorEditor" : "###AnimatorEditor");

        ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            drawMenuBar();
            drawToolbar();

            ImVec2 contentSize = ImGui::GetContentRegionAvail();

            ImGui::BeginChild("MainContent", ImVec2(0, contentSize.y), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 innerSize = ImGui::GetContentRegionAvail();
                float panelWidth = 300.0f;
                float graphWidth = innerSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;

                // Save previous layer index to detect changes
                uint32_t layerIndexBeforePanel = selectedLayerIndex;

                ImGui::BeginChild("PropertiesPanel", ImVec2(panelWidth, innerSize.y), true);

                // Layers section
                if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    layerPanel.draw(animatorData.get(), selectedLayerIndex, isDirty);
                }

                ImGui::Separator();

                // Bone Masks section
                if (ImGui::CollapsingHeader("Bone Masks"))
                {
                    boneMaskPanel.draw(animatorData.get(), isDirty);
                }

                ImGui::Separator();

                // Sync graph from selected layer AFTER layer panel (selection may have changed)
                if (selectedLayerIndex != layerIndexBeforePanel)
                {
                    // Layer changed: sync old edits back, then load new layer's graph
                    if (animatorData && !animatorData->layers.empty())
                    {
                        if (layerIndexBeforePanel < animatorData->layers.size())
                        {
                            animatorData->layers[layerIndexBeforePanel].graph = animatorData->graph;
                        }
                        if (selectedLayerIndex < animatorData->layers.size())
                        {
                            animatorData->graph = animatorData->layers[selectedLayerIndex].graph;
                        }
                    }
                    selectedStateId = 0;
                    selectedTransitionId = 0;
                    needsPositionInit = true;
                    needsNavigateToContent = true;
                }
                else if (animatorData && !animatorData->layers.empty() && selectedLayerIndex < animatorData->layers.size())
                {
                    // Same layer: just sync from layer (first frame or after reload)
                    animatorData->graph = animatorData->layers[selectedLayerIndex].graph;
                }

                if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    propertiesPanel.drawParametersPanel(animatorData.get(), isDirty, showAddParameterPopup);
                }

                ImGui::Separator();

                if (selectedStateId != 0)
                {
                    if (ImGui::CollapsingHeader("State Properties", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        propertiesPanel.drawStatePropertiesPanel(animatorData.get(), selectedStateId, isDirty);
                    }
                }

                if (selectedTransitionId != 0)
                {
                    if (ImGui::CollapsingHeader("Transition Properties", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        propertiesPanel.drawTransitionPropertiesPanel(animatorData.get(), selectedTransitionId, isDirty);
                    }
                }

                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("NodeGraphPanel", ImVec2(graphWidth, innerSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                nodeGraph.draw(animatorData.get(), selectedStateId, selectedTransitionId,
                               isDirty, needsPositionInit, needsNavigateToContent, pendingZoomSteps);

                // Sync changes back to the selected layer
                if (animatorData && !animatorData->layers.empty() && selectedLayerIndex < animatorData->layers.size())
                {
                    animatorData->layers[selectedLayerIndex].graph = animatorData->graph;
                }

                propertiesPanel.drawAddParameterPopup(animatorData.get(), showAddParameterPopup,
                                                       newParameterName, newParameterType, isDirty);
                ImGui::EndChild();
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void AnimatorEditorWindow::drawMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveAnimator();
                }
                if (ImGui::MenuItem("Reload"))
                {
                    loadAnimator();
                    isDirty = false;
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
                if (ImGui::MenuItem("Add State"))
                {
                    auto* graph = getActiveGraph();
                    if (graph)
                    {
                        animator::AnimatorState state;
                        state.id = graph->nextStateId++;
                        state.name = "New State " + std::to_string(state.id);
                        state.position = glm::vec2(200.0f, 100.0f);
                        graph->states.push_back(std::move(state));
                        isDirty = true;
                    }
                }
                if (ImGui::MenuItem("Add Parameter"))
                {
                    showAddParameterPopup = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
    }

    void AnimatorEditorWindow::drawToolbar()
    {
        if (ImGui::Button("Save"))
        {
            saveAnimator();
        }
        ImGui::SameLine();

        if (ImGui::Button("Add State"))
        {
            auto* graph = getActiveGraph();
            if (graph)
            {
                animator::AnimatorState state;
                state.id = graph->nextStateId++;
                state.name = "New State " + std::to_string(state.id);
                state.position = glm::vec2(200.0f + (graph->states.size() * 50.0f), 100.0f);
                graph->states.push_back(std::move(state));
                isDirty = true;
            }
        }

        ImGui::Separator();
    }
}
