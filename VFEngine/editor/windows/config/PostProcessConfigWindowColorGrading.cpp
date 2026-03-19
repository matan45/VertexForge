#include "PostProcessConfigWindow.hpp"
#include <imgui.h>
#include <cstring>
#include <fstream>
#include "nfd/FileDialog.hpp"

namespace windows
{
    void PostProcessConfigWindow::drawColorGradingSection()
    {
        if (ImGui::CollapsingHeader("Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Color Grading", &settings.colorGrading.enabled))
            {
                isDirty = true;
            }

            if (settings.colorGrading.enabled)
            {
                ImGui::Spacing();

                ImGui::SeparatorText("LUT");

                {
                    char primaryBuf[512] = {};
                    std::strncpy(primaryBuf, settings.colorGrading.primaryLutPath.c_str(), sizeof(primaryBuf) - 1);
                    if (ImGui::InputText("##PrimaryLUT", primaryBuf, sizeof(primaryBuf)))
                    {
                        settings.colorGrading.primaryLutPath = primaryBuf;
                        isDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##Primary"))
                    {
                        nfd::FileDialog fileDialog;
                        std::string path = fileDialog.openFileDialog({{L"Cube LUT", L"*.cube"}});
                        if (!path.empty())
                        {
                            settings.colorGrading.primaryLutPath = path;
                            isDirty = true;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted("Primary LUT");
                }

                {
                    char secondaryBuf[512] = {};
                    std::strncpy(secondaryBuf, settings.colorGrading.secondaryLutPath.c_str(), sizeof(secondaryBuf) - 1);
                    if (ImGui::InputText("##SecondaryLUT", secondaryBuf, sizeof(secondaryBuf)))
                    {
                        settings.colorGrading.secondaryLutPath = secondaryBuf;
                        isDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##Secondary"))
                    {
                        nfd::FileDialog fileDialog;
                        std::string path = fileDialog.openFileDialog({{L"Cube LUT", L"*.cube"}});
                        if (!path.empty())
                        {
                            settings.colorGrading.secondaryLutPath = path;
                            isDirty = true;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted("Secondary LUT");
                }

                if (ImGui::Button("Export Identity LUT"))
                {
                    nfd::FileDialog fileDialog;
                    std::string path = fileDialog.saveFileDialog({{L"Cube LUT", L"*.cube"}}, L"cube");
                    if (!path.empty())
                    {
                        const uint32_t size = 32;
                        std::ofstream file(path);
                        if (file.is_open())
                        {
                            file << "# Identity LUT - edit in Photoshop/DaVinci Resolve then reload\n";
                            file << "LUT_3D_SIZE " << size << "\n";
                            file << "DOMAIN_MIN 0.0 0.0 0.0\n";
                            file << "DOMAIN_MAX 1.0 1.0 1.0\n";
                            for (uint32_t b = 0; b < size; b++)
                            {
                                for (uint32_t g = 0; g < size; g++)
                                {
                                    for (uint32_t r = 0; r < size; r++)
                                    {
                                        file << static_cast<float>(r) / static_cast<float>(size - 1) << " "
                                             << static_cast<float>(g) / static_cast<float>(size - 1) << " "
                                             << static_cast<float>(b) / static_cast<float>(size - 1) << "\n";
                                    }
                                }
                            }
                        }
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Save a neutral identity .cube file to edit externally");

                if (ImGui::DragFloat("LUT Intensity", &settings.colorGrading.lutIntensity, 0.01f, 0.0f, 1.0f, "%.2f"))
                    isDirty = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 = no LUT effect, 1 = full LUT");

                if (!settings.colorGrading.secondaryLutPath.empty())
                {
                    if (ImGui::DragFloat("LUT Blend", &settings.colorGrading.lutBlendFactor, 0.01f, 0.0f, 1.0f, "%.2f"))
                        isDirty = true;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("0 = primary only, 1 = secondary only");
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Manual Adjustments");

                float lift[3] = {settings.colorGrading.liftR, settings.colorGrading.liftG, settings.colorGrading.liftB};
                if (ImGui::DragFloat3("Lift (Shadows)", lift, 0.01f, -1.0f, 1.0f, "%.3f"))
                {
                    settings.colorGrading.liftR = lift[0];
                    settings.colorGrading.liftG = lift[1];
                    settings.colorGrading.liftB = lift[2];
                    isDirty = true;
                }

                float gamma[3] = {settings.colorGrading.gammaR, settings.colorGrading.gammaG, settings.colorGrading.gammaB};
                if (ImGui::DragFloat3("Gamma (Midtones)", gamma, 0.01f, 0.01f, 5.0f, "%.3f"))
                {
                    settings.colorGrading.gammaR = gamma[0];
                    settings.colorGrading.gammaG = gamma[1];
                    settings.colorGrading.gammaB = gamma[2];
                    isDirty = true;
                }

                float gain[3] = {settings.colorGrading.gainR, settings.colorGrading.gainG, settings.colorGrading.gainB};
                if (ImGui::DragFloat3("Gain (Highlights)", gain, 0.01f, 0.0f, 5.0f, "%.3f"))
                {
                    settings.colorGrading.gainR = gain[0];
                    settings.colorGrading.gainG = gain[1];
                    settings.colorGrading.gainB = gain[2];
                    isDirty = true;
                }

                if (ImGui::DragFloat("Saturation", &settings.colorGrading.saturation, 0.01f, 0.0f, 3.0f, "%.2f"))
                    isDirty = true;

                if (ImGui::DragFloat("Color Temperature", &settings.colorGrading.colorTemperature, 50.0f, 1000.0f, 15000.0f, "%.0f K"))
                    isDirty = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Color temperature in Kelvin. 6500K = daylight (neutral).\nLower = warmer (orange), Higher = cooler (blue).");

                if (ImGui::DragFloat("Color Tint", &settings.colorGrading.colorTint, 0.01f, -1.0f, 1.0f, "%.2f"))
                    isDirty = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Green-Magenta tint adjustment");
            }

            ImGui::Unindent(10.0f);
        }
    }
}
