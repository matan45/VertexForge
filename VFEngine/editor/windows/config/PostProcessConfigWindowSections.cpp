#include "PostProcessConfigWindow.hpp"
#include <imgui.h>

namespace windows
{
    void PostProcessConfigWindow::drawDepthOfFieldSection()
    {
        if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Depth of Field", &settings.depthOfField.enabled))
            {
                isDirty = true;
            }

            if (settings.depthOfField.enabled)
            {
                ImGui::Spacing();

                const char* focusModes[] = {"Manual", "Target Point"};
                int focusModeIdx = static_cast<int>(settings.depthOfField.focusMode);
                if (ImGui::Combo("Focus Mode", &focusModeIdx, focusModes, IM_ARRAYSIZE(focusModes)))
                {
                    settings.depthOfField.focusMode = static_cast<::postprocess::DoFFocusMode>(focusModeIdx);
                    isDirty = true;
                }

                if (settings.depthOfField.focusMode == ::postprocess::DoFFocusMode::Manual)
                {
                    if (ImGui::DragFloat("Focal Distance", &settings.depthOfField.focalDistance, 0.1f, 0.1f, 1000.0f, "%.1f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Distance at which objects are in perfect focus.");
                    }
                }
                else
                {
                    float focusTarget[3] = {settings.depthOfField.focusTargetX,
                                            settings.depthOfField.focusTargetY,
                                            settings.depthOfField.focusTargetZ};
                    if (ImGui::DragFloat3("Focus Target", focusTarget, 0.1f))
                    {
                        settings.depthOfField.focusTargetX = focusTarget[0];
                        settings.depthOfField.focusTargetY = focusTarget[1];
                        settings.depthOfField.focusTargetZ = focusTarget[2];
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("World-space position to focus on.");
                    }

                    if (ImGui::DragFloat("Focus Smoothing", &settings.depthOfField.focusSmoothing, 0.1f, 0.1f, 50.0f, "%.1f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("How quickly focus transitions to the target.\nHigher = faster.");
                    }
                }

                if (ImGui::DragFloat("Focal Range", &settings.depthOfField.focalRange, 0.1f, 0.1f, 100.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Range around the focal distance that remains sharp.\nSmaller = shallower depth of field.");
                }

                if (ImGui::DragFloat("Max Blur Radius", &settings.depthOfField.maxBlurRadius, 0.1f, 0.0f, 20.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Maximum blur amount in pixels for fully out-of-focus areas.");
                }

                int samples = settings.depthOfField.sampleCount;
                if (ImGui::SliderInt("Samples##dof", &samples, 4, 32))
                {
                    settings.depthOfField.sampleCount = samples;
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Number of Poisson disc samples.\nMore = smoother blur but slower.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawSSAOSection()
    {
        if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable SSAO", &settings.ssao.enabled))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Screen-Space Ambient Occlusion.\nDarkens corners and crevices for added depth.");
            }

            if (settings.ssao.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Radius##ssao", &settings.ssao.radius, 0.01f, 0.1f, 5.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("World-space sample radius.\nLarger = wider AO effect.");
                }

                if (ImGui::DragFloat("Bias##ssao", &settings.ssao.bias, 0.001f, 0.001f, 0.1f, "%.3f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Depth comparison bias.\nIncrease to reduce self-occlusion artifacts.");
                }

                if (ImGui::DragFloat("Intensity##ssao", &settings.ssao.intensity, 0.01f, 0.1f, 5.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("AO darkening strength.\nHigher = darker ambient occlusion.");
                }

                if (ImGui::SliderInt("Kernel Size##ssao", &settings.ssao.kernelSize, 8, 64))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Number of hemisphere samples.\nMore = better quality but slower.");
                }

                if (ImGui::DragFloat("Power##ssao", &settings.ssao.power, 0.01f, 0.5f, 5.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Contrast power curve.\nHigher = sharper AO transitions.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawEdgeDetectionSection()
    {
        if (ImGui::CollapsingHeader("Edge Detection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Edge Detection", &settings.edgeDetection.enabled))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Sobel-based edge detection on depth buffer.\nDraws outlines on geometric edges.");
            }

            if (settings.edgeDetection.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Threshold##edge", &settings.edgeDetection.threshold, 0.001f, 0.01f, 1.0f, "%.3f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Edge sensitivity.\nLower = more edges detected.");
                }

                if (ImGui::DragFloat("Edge Width##edge", &settings.edgeDetection.edgeWidth, 0.1f, 0.5f, 3.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Edge line thickness.");
                }

                if (ImGui::ColorEdit3("Edge Color##edge", settings.edgeDetection.edgeColor))
                {
                    isDirty = true;
                }

                if (ImGui::SliderFloat("Opacity##edge", &settings.edgeDetection.opacity, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Edge overlay opacity.\n0 = invisible, 1 = fully opaque.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }
}
