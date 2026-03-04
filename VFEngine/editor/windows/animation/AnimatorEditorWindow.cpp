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

    void AnimatorEditorWindow::draw()
    {
        if (needsInit)
        {
            initEditor();
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

                ImGui::BeginChild("PropertiesPanel", ImVec2(panelWidth, innerSize.y), true);

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
                    if (animatorData)
                    {
                        animator::AnimatorState state;
                        state.id = animatorData->graph.nextStateId++;
                        state.name = "New State " + std::to_string(state.id);
                        state.position = glm::vec2(200.0f, 100.0f);
                        animatorData->graph.states.push_back(std::move(state));
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
            if (animatorData)
            {
                animator::AnimatorState state;
                state.id = animatorData->graph.nextStateId++;
                state.name = "New State " + std::to_string(state.id);
                state.position = glm::vec2(200.0f + (animatorData->graph.states.size() * 50.0f), 100.0f);
                animatorData->graph.states.push_back(std::move(state));
                isDirty = true;
            }
        }

        ImGui::Separator();
    }
}
