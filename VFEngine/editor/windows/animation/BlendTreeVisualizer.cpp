#include "BlendTreeVisualizer.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace windows::animation
{
    void BlendTreeVisualizer::draw1D(const animator::BlendTreeData& blendTree,
                                      float currentParamValue,
                                      const ImVec2& availSize)
    {
        if (blendTree.entries.empty())
        {
            ImGui::TextDisabled("No blend entries");
            return;
        }

        float height = std::max(availSize.y, 60.0f);
        float width = std::max(availSize.x, 100.0f);

        ImGui::BeginChild("BlendTree1D", ImVec2(width, height), true);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float padding = 20.0f;
        float axisY = pos.y + height * 0.5f;
        float axisLeft = pos.x + padding;
        float axisRight = pos.x + width - padding;
        float axisWidth = axisRight - axisLeft;

        // Find threshold range
        float minThreshold = blendTree.entries[0].threshold;
        float maxThreshold = blendTree.entries[0].threshold;
        for (const auto& entry : blendTree.entries)
        {
            minThreshold = std::min(minThreshold, entry.threshold);
            maxThreshold = std::max(maxThreshold, entry.threshold);
        }

        float range = maxThreshold - minThreshold;
        if (range < 0.001f) range = 1.0f;
        float rangeMin = minThreshold - range * 0.1f;
        float rangeMax = maxThreshold + range * 0.1f;
        float fullRange = rangeMax - rangeMin;

        auto mapX = [&](float value) -> float {
            return axisLeft + ((value - rangeMin) / fullRange) * axisWidth;
        };

        // Draw axis line
        drawList->AddLine(ImVec2(axisLeft, axisY), ImVec2(axisRight, axisY),
                          IM_COL32(150, 150, 150, 200), 2.0f);

        // Draw threshold markers
        for (const auto& entry : blendTree.entries)
        {
            float x = mapX(entry.threshold);
            float tickTop = axisY - 12.0f;
            float tickBottom = axisY + 12.0f;

            drawList->AddLine(ImVec2(x, tickTop), ImVec2(x, tickBottom),
                              IM_COL32(200, 200, 200, 220), 2.0f);
            drawList->AddCircleFilled(ImVec2(x, axisY), 5.0f, IM_COL32(100, 160, 220, 255));

            // Label
            char label[128];
            if (entry.animationRef.isValid())
            {
                fs::path animPath(entry.animationRef.resolve());
                snprintf(label, sizeof(label), "%s\n%.2f", animPath.filename().string().c_str(), entry.threshold);
            }
            else
            {
                snprintf(label, sizeof(label), "%.2f", entry.threshold);
            }

            ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(x - textSize.x * 0.5f, tickBottom + 2.0f),
                              IM_COL32(180, 180, 180, 255), label);
        }

        // Draw current parameter value indicator
        float currentX = mapX(currentParamValue);
        currentX = std::clamp(currentX, axisLeft, axisRight);
        drawList->AddTriangleFilled(
            ImVec2(currentX, axisY - 16.0f),
            ImVec2(currentX - 6.0f, axisY - 24.0f),
            ImVec2(currentX + 6.0f, axisY - 24.0f),
            IM_COL32(255, 200, 50, 255));
        drawList->AddCircleFilled(ImVec2(currentX, axisY), 7.0f, IM_COL32(255, 200, 50, 255));

        // Parameter name label
        char paramLabel[64];
        snprintf(paramLabel, sizeof(paramLabel), "%s = %.2f", blendTree.parameterName.c_str(), currentParamValue);
        drawList->AddText(ImVec2(axisLeft, pos.y + 2.0f), IM_COL32(255, 200, 50, 255), paramLabel);

        ImGui::EndChild();
    }

    void BlendTreeVisualizer::draw2D(const animator::BlendTreeData& blendTree,
                                      float currentParamX, float currentParamY,
                                      const ImVec2& availSize)
    {
        if (blendTree.entries.empty())
        {
            ImGui::TextDisabled("No blend entries");
            return;
        }

        float size = std::min(std::max(availSize.x, 100.0f), std::max(availSize.y, 100.0f));

        ImGui::BeginChild("BlendTree2D", ImVec2(size, size), true);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float padding = 30.0f;
        float innerSize = size - padding * 2.0f;

        // Find bounds
        float minX = blendTree.entries[0].position.x, maxX = minX;
        float minY = blendTree.entries[0].position.y, maxY = minY;
        for (const auto& entry : blendTree.entries)
        {
            minX = std::min(minX, entry.position.x);
            maxX = std::max(maxX, entry.position.x);
            minY = std::min(minY, entry.position.y);
            maxY = std::max(maxY, entry.position.y);
        }

        float rangeX = maxX - minX;
        float rangeY = maxY - minY;
        if (rangeX < 0.001f) rangeX = 1.0f;
        if (rangeY < 0.001f) rangeY = 1.0f;
        float padFrac = 0.15f;
        float fullMinX = minX - rangeX * padFrac;
        float fullMaxX = maxX + rangeX * padFrac;
        float fullMinY = minY - rangeY * padFrac;
        float fullMaxY = maxY + rangeY * padFrac;
        float fullRangeX = fullMaxX - fullMinX;
        float fullRangeY = fullMaxY - fullMinY;

        auto mapPosX = [&](float v) -> float {
            return pos.x + padding + ((v - fullMinX) / fullRangeX) * innerSize;
        };
        auto mapPosY = [&](float v) -> float {
            return pos.y + padding + innerSize - ((v - fullMinY) / fullRangeY) * innerSize;
        };

        // Draw grid
        ImU32 gridColor = IM_COL32(60, 60, 60, 150);
        for (int i = 0; i <= 4; ++i)
        {
            float t = static_cast<float>(i) / 4.0f;
            float gx = pos.x + padding + t * innerSize;
            float gy = pos.y + padding + t * innerSize;
            drawList->AddLine(ImVec2(gx, pos.y + padding), ImVec2(gx, pos.y + padding + innerSize), gridColor);
            drawList->AddLine(ImVec2(pos.x + padding, gy), ImVec2(pos.x + padding + innerSize, gy), gridColor);
        }

        // Draw axes labels
        drawList->AddText(ImVec2(pos.x + padding + innerSize * 0.5f - 20.0f, pos.y + padding + innerSize + 4.0f),
                          IM_COL32(180, 180, 180, 255), blendTree.parameterName.c_str());
        drawList->AddText(ImVec2(pos.x + 2.0f, pos.y + padding + innerSize * 0.5f),
                          IM_COL32(180, 180, 180, 255), blendTree.parameterNameY.c_str());

        // Draw sample points
        for (const auto& entry : blendTree.entries)
        {
            float ex = mapPosX(entry.position.x);
            float ey = mapPosY(entry.position.y);

            drawList->AddCircleFilled(ImVec2(ex, ey), 8.0f, IM_COL32(100, 160, 220, 200));
            drawList->AddCircle(ImVec2(ex, ey), 8.0f, IM_COL32(150, 200, 255, 255), 0, 1.5f);

            if (entry.animationRef.isValid())
            {
                fs::path animPath(entry.animationRef.resolve());
                std::string label = animPath.filename().string();
                ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                drawList->AddText(ImVec2(ex - textSize.x * 0.5f, ey + 10.0f),
                                  IM_COL32(180, 180, 180, 255), label.c_str());
            }
        }

        // Draw current blend position as crosshair
        float cx = mapPosX(currentParamX);
        float cy = mapPosY(currentParamY);
        cx = std::clamp(cx, pos.x + padding, pos.x + padding + innerSize);
        cy = std::clamp(cy, pos.y + padding, pos.y + padding + innerSize);

        float crossSize = 10.0f;
        ImU32 crossColor = IM_COL32(255, 200, 50, 255);
        drawList->AddLine(ImVec2(cx - crossSize, cy), ImVec2(cx + crossSize, cy), crossColor, 2.0f);
        drawList->AddLine(ImVec2(cx, cy - crossSize), ImVec2(cx, cy + crossSize), crossColor, 2.0f);
        drawList->AddCircleFilled(ImVec2(cx, cy), 5.0f, IM_COL32(255, 200, 50, 200));

        ImGui::EndChild();
    }
}
