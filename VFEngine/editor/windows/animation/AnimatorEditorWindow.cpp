#include "print/Log.hpp"
#include "AnimatorEditorWindow.hpp"
#include "animator/AnimatorAsset.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "imgui.h"
#include <imgui_internal.h>
#include <imgui_node_editor.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <cctype>

namespace ed = ax::NodeEditor;

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
            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = animatorPath;
            events::EventDispatcher::instance().publish(assetNotif);
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
        isDirty = true;
    }

    void AnimatorEditorWindow::draw()
    {
        if (needsInit)
        {
            initEditor();
            ensureLayersInitialized();
        }

        // Per-asset ID so two open animators don't merge into one window.
        std::string title = windowTitle + (isDirty ? " *###Animator_" : "###Animator_") + animatorPath;

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("AnimatorEditor", ImVec2(1200, 800));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            drawMenuBar();
            drawToolbar();

            ImVec2 contentSize = ImGui::GetContentRegionAvail();

            ImGui::BeginChild("MainContent", ImVec2(0, contentSize.y), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 innerSize = ImGui::GetContentRegionAvail();
                static float panelWidth = 300.0f;
                const float splitterThickness = 5.0f;
                panelWidth = std::clamp(panelWidth, 220.0f,
                                        std::max(220.0f, innerSize.x - 300.0f - splitterThickness));
                float graphWidth = innerSize.x - panelWidth - splitterThickness;

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
                            std::swap(animatorData->graph, animatorData->layers[layerIndexBeforePanel].graph);
                        }
                        if (selectedLayerIndex < animatorData->layers.size())
                        {
                            std::swap(animatorData->graph, animatorData->layers[selectedLayerIndex].graph);
                        }
                    }
                    selectedStateId = 0;
                    selectedTransitionId = 0;
                    needsPositionInit = true;
                    needsNavigateToContent = true;
                }
                else if (animatorData && !animatorData->layers.empty() && selectedLayerIndex < animatorData->layers.size())
                {
                    // Same layer: swap layer graph in for editing
                    std::swap(animatorData->graph, animatorData->layers[selectedLayerIndex].graph);
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

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##animatorSplit", splitterThickness, &panelWidth,
                                           &graphWidth, 220.0f, 300.0f, innerSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild("NodeGraphPanel", ImVec2(graphWidth, innerSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                nodeGraph.draw(animatorData.get(), selectedStateId, selectedTransitionId,
                               isDirty, needsPositionInit, needsNavigateToContent, pendingZoomSteps);

                // Swap the edited graph back into the selected layer
                if (animatorData && !animatorData->layers.empty() && selectedLayerIndex < animatorData->layers.size())
                {
                    std::swap(animatorData->graph, animatorData->layers[selectedLayerIndex].graph);
                }

                propertiesPanel.drawAddParameterPopup(animatorData.get(), showAddParameterPopup,
                                                       newParameterName, newParameterType, isDirty);
                ImGui::EndChild();
            }
            ImGui::EndChild();
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("AnimatorEditor", maximizer.effectiveSize());
            sizeSaved = true;
        }
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

            maximizer.drawButton();

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

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Search
        ImGui::SetNextItemWidth(180.0f);
        const bool searchChanged = ImGui::InputTextWithHint(
            "##Search", ICON_FA_MAGNIFYING_GLASS " Search states by name...",
            searchBuffer, sizeof(searchBuffer));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Type to find a state by name and jump to it in the graph");
        if (searchChanged)
        {
            // Navigate to first matching state
            if (searchBuffer[0] != '\0' && animatorData)
            {
                std::string query(searchBuffer);
                for (auto& c : query) c = static_cast<char>(std::tolower(c));

                for (const auto& state : animatorData->graph.states)
                {
                    std::string name = state.name;
                    for (auto& c : name) c = static_cast<char>(std::tolower(c));

                    if (name.find(query) != std::string::npos)
                    {
                        selectedStateId = state.id;
                        auto* ctx = nodeGraph.getContext();
                        if (ctx)
                        {
                            ed::SetCurrentEditor(ctx);
                            ed::SelectNode(ed::NodeId(static_cast<uintptr_t>(state.id + 1000)), false);
                            ed::NavigateToSelection();
                            ed::SetCurrentEditor(nullptr);
                        }
                        break;
                    }
                }
            }
        }

        ImGui::SameLine();

        // Validate graph
        if (ImGui::SmallButton(ICON_FA_TRIANGLE_EXCLAMATION " Validate"))
        {
            if (animatorData)
            {
                validationWarnings = animation::AnimatorGraphValidator::validate(*animatorData);
                showValidationPanel = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Check the graph for missing default state, dead ends, "
                              "unreachable states, missing clips, and undefined parameters");

        // Status badge
        if (showValidationPanel)
        {
            ImGui::SameLine();
            if (validationWarnings.empty())
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                                   ICON_FA_CIRCLE_CHECK " No issues");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "(%zu)", validationWarnings.size());
        }

        ImGui::Separator();

        // Validation panel - only shown when there are warnings to display.
        // On success the inline "No issues" badge above is enough; no need for an empty panel.
        if (showValidationPanel && !validationWarnings.empty())
        {
            ImGui::BeginChild("ValidationPanel", ImVec2(0, 80), true);
            for (const auto& warning : validationWarnings)
            {
                ImVec4 color = (warning.level == animation::GraphWarning::Level::Error)
                    ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f)
                    : ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
                const char* icon = (warning.level == animation::GraphWarning::Level::Error)
                    ? ICON_FA_CIRCLE_EXCLAMATION : ICON_FA_TRIANGLE_EXCLAMATION;
                ImGui::TextColored(color, "%s %s", icon, warning.message.c_str());
            }
            ImGui::EndChild();
        }
    }
}
