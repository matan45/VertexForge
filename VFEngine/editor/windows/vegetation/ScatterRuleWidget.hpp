#pragma once
// VK-1585: shared per-rule editor for a vegetation::ScatterRule. Used by BOTH the grass
// (GrassDensityPanel) and foliage (FoliageBrushToolPanel) scatter UIs so the rich gate editor
// (slope / height / noise / layer / curvature) lives in exactly one place and cannot drift.
#include "vegetation/VegetationScatterTypes.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace windows
{
    // Draws one ScatterRule inside a collapsible TreeNode. `paletteCount` bounds the palette-entry
    // picker (billboard entries for grass, FoliageType entries for foliage). `idBase` namespaces the
    // ImGui IDs so two rule lists on one panel never collide. Sets `changed` on any edit and sets
    // `removeIndex = index` if the user clicks Remove.
    inline void drawScatterRuleEditor(int index, vegetation::ScatterRule& r, int paletteCount,
                                      int idBase, int& removeIndex, bool& changed)
    {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        ImGui::PushID(idBase + index);

        std::string label = "Rule " + std::to_string(index) +
                            " -> entry " + std::to_string(r.paletteEntryIndex);
        if (ImGui::TreeNode("Rule", "%s", label.c_str()))
        {
            int pe = static_cast<int>(r.paletteEntryIndex);
            const int maxEntry = paletteCount <= 0 ? 0 : paletteCount - 1;
            if (ImGui::DragInt("Palette Entry", &pe, 0.1f, 0, maxEntry))
            {
                r.paletteEntryIndex = static_cast<uint32_t>(std::clamp(pe, 0, maxEntry));
                changed = true;
            }

            changed |= ImGui::SliderFloat("Density", &r.density, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Spacing", &r.spacing, 0.1f, 10.0f);
            changed |= ImGui::SliderFloat("Jitter", &r.positionJitter, 0.0f, 1.0f);
            changed |= ImGui::Checkbox("Align To Normal", &r.alignToNormal);

            changed |= ImGui::Checkbox("Slope Mask", &r.useSlopeMask);
            if (r.useSlopeMask)
            {
                // Present as degrees like the brush; store as cosine (bounds invert).
                float minDeg = std::acos(std::clamp(r.slopeMaxCos, 0.0f, 1.0f)) / kDegToRad;
                float maxDeg = std::acos(std::clamp(r.slopeMinCos, 0.0f, 1.0f)) / kDegToRad;
                bool slopeChanged = false;
                slopeChanged |= ImGui::SliderFloat("Min Slope (deg)", &minDeg, 0.0f, 90.0f);
                slopeChanged |= ImGui::SliderFloat("Max Slope (deg)", &maxDeg, 0.0f, 90.0f);
                if (slopeChanged)
                {
                    if (maxDeg < minDeg) maxDeg = minDeg;
                    r.slopeMinCos = std::cos(maxDeg * kDegToRad);
                    r.slopeMaxCos = std::cos(minDeg * kDegToRad);
                    changed = true;
                }
            }

            changed |= ImGui::Checkbox("Height Mask", &r.useHeightMask);
            if (r.useHeightMask)
            {
                changed |= ImGui::DragFloat("Min Height", &r.heightMin, 0.5f);
                changed |= ImGui::DragFloat("Max Height", &r.heightMax, 0.5f);
            }

            changed |= ImGui::Checkbox("Noise Mask", &r.useNoiseMask);
            if (r.useNoiseMask)
            {
                changed |= ImGui::SliderFloat("Noise Freq", &r.noiseFrequency, 0.01f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat("Noise Threshold", &r.noiseThreshold, 0.0f, 1.0f);
                int ns = static_cast<int>(r.noiseSeed);
                if (ImGui::DragInt("Noise Seed", &ns, 1.0f, 0, 1000000))
                {
                    r.noiseSeed = static_cast<uint32_t>(ns < 0 ? 0 : ns);
                    changed = true;
                }
            }

            changed |= ImGui::Checkbox("Layer Mask", &r.useLayerMask);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Gate placement by a terrain splat/material layer weight");
            if (r.useLayerMask)
            {
                int li = static_cast<int>(r.layerIndex);
                if (ImGui::DragInt("Layer Index", &li, 1.0f, 0, 31))
                {
                    r.layerIndex = static_cast<uint8_t>(std::clamp(li, 0, 31));
                    changed = true;
                }
                changed |= ImGui::SliderFloat("Min Weight", &r.layerWeightMin, 0.0f, 1.0f);
                changed |= ImGui::Checkbox("Invert (exclude)", &r.invertLayer);
            }

            changed |= ImGui::Checkbox("Curvature Mask", &r.useCurvatureMask);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Gate by terrain concavity (world-Y units): + = hollows/valleys, - = ridges/peaks");
            if (r.useCurvatureMask)
            {
                changed |= ImGui::DragFloat("Curv Min (concave +)", &r.curvatureMin, 0.05f);
                changed |= ImGui::DragFloat("Curv Max", &r.curvatureMax, 0.05f);
            }

            if (ImGui::Button("Remove Rule"))
                removeIndex = index;

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    // Draws the biome-layer list editor (VK-1585): per-biome name / splat layer / priority / edge
    // blend / density plus a nested ScatterRule list (reusing drawScatterRuleEditor). An empty list
    // means flat-rules-only. Sets `changed` on any edit. paletteCount bounds the rule palette pickers.
    inline void drawBiomeListEditor(std::vector<vegetation::BiomeLayer>& biomes, int paletteCount, bool& changed)
    {
        if (!ImGui::TreeNode("Biomes (layered)"))
            return;

        ImGui::TextDisabled("Composite rule sets by terrain layer with soft edge-blend.");

        int removeBiome = -1;
        for (int b = 0; b < static_cast<int>(biomes.size()); ++b)
        {
            auto& biome = biomes[b];
            ImGui::PushID(5000 + b);

            std::string blabel = biome.name.empty() ? ("Biome " + std::to_string(b)) : biome.name;
            if (ImGui::TreeNode("Biome", "%s", blabel.c_str()))
            {
                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", biome.name.c_str());
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                {
                    biome.name = nameBuf;
                    changed = true;
                }

                int li = static_cast<int>(biome.biomeLayerIndex);
                if (ImGui::DragInt("Biome Layer", &li, 1.0f, 0, 31))
                {
                    biome.biomeLayerIndex = static_cast<uint8_t>(std::clamp(li, 0, 31));
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Terrain splat layer that defines where this biome applies");
                changed |= ImGui::DragInt("Priority", &biome.priority, 1.0f);
                changed |= ImGui::SliderFloat("Edge Blend", &biome.edgeBlendWidth, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Biome Density", &biome.densityScale, 0.0f, 1.0f);

                int removeRule = -1;
                for (int i = 0; i < static_cast<int>(biome.rules.size()); ++i)
                    drawScatterRuleEditor(i, biome.rules[i], paletteCount, 6000 + b * 100, removeRule, changed);
                if (removeRule >= 0)
                {
                    biome.rules.erase(biome.rules.begin() + removeRule);
                    changed = true;
                }
                if (ImGui::Button("Add Rule##biome"))
                {
                    biome.rules.emplace_back();
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Biome"))
                    removeBiome = b;

                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (removeBiome >= 0)
        {
            biomes.erase(biomes.begin() + removeBiome);
            changed = true;
        }
        if (ImGui::Button("Add Biome"))
        {
            biomes.emplace_back();
            changed = true;
        }

        ImGui::TreePop();
    }
}
