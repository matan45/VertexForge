#include "VFXPropertyPanel.hpp"
#include "imgui.h"
#include <nfd/FileDialog.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace editor::vfxeditor
{
    void VFXCurveDelegate::syncFrom(const vfx::VFXCurve& curve, float yMin, float yMax)
    {
        points.clear();
        for (const auto& key : curve.keys)
        {
            points.push_back({key.time, key.value});
        }
        rangeMin = ImVec2(0.0f, yMin);
        rangeMax = ImVec2(1.0f, yMax);
        dirty = false;
    }

    void VFXCurveDelegate::syncTo(vfx::VFXCurve& curve)
    {
        curve.keys.clear();
        for (const auto& p : points)
        {
            vfx::VFXCurveKey key;
            key.time = p.x;
            key.value = p.y;
            curve.keys.push_back(key);
        }
        std::sort(curve.keys.begin(), curve.keys.end(),
                  [](const auto& a, const auto& b) { return a.time < b.time; });

        if (curve.keys.size() <= 1)
        {
            for (auto& key : curve.keys)
            {
                key.inTangent = 0.0f;
                key.outTangent = 0.0f;
            }
        }
        else
        {
            for (size_t i = 0; i < curve.keys.size(); ++i)
            {
                if (i == 0)
                {
                    float dt = curve.keys[1].time - curve.keys[0].time;
                    curve.keys[i].outTangent = (dt > 0.0001f)
                        ? (curve.keys[1].value - curve.keys[0].value) / dt : 0.0f;
                    curve.keys[i].inTangent = curve.keys[i].outTangent;
                }
                else if (i == curve.keys.size() - 1)
                {
                    float dt = curve.keys[i].time - curve.keys[i - 1].time;
                    curve.keys[i].inTangent = (dt > 0.0001f)
                        ? (curve.keys[i].value - curve.keys[i - 1].value) / dt : 0.0f;
                    curve.keys[i].outTangent = curve.keys[i].inTangent;
                }
                else
                {
                    float dt = curve.keys[i + 1].time - curve.keys[i - 1].time;
                    if (dt > 0.0001f)
                    {
                        float tangent = (curve.keys[i + 1].value - curve.keys[i - 1].value) / dt;
                        curve.keys[i].inTangent = tangent;
                        curve.keys[i].outTangent = tangent;
                    }
                    else
                    {
                        curve.keys[i].inTangent = 0.0f;
                        curve.keys[i].outTangent = 0.0f;
                    }
                }
            }
        }
        dirty = false;
    }

    int VFXCurveDelegate::EditPoint(size_t, int pointIndex, ImVec2 value)
    {
        points[pointIndex] = value;
        dirty = true;
        return pointIndex;
    }

    void VFXCurveDelegate::AddPoint(size_t, ImVec2 value)
    {
        points.push_back(value);
        dirty = true;
    }

    void VFXGradientDelegate::syncFrom(const vfx::VFXGradient& gradient)
    {
        points.clear();
        alphas.clear();
        for (const auto& stop : gradient.stops)
        {
            points.push_back({stop.color.r, stop.color.g, stop.color.b, stop.position});
            alphas.push_back(stop.color.a);
        }
        dirty = false;
    }

    void VFXGradientDelegate::syncTo(vfx::VFXGradient& gradient)
    {
        gradient.stops.clear();
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto& p = points[i];
            vfx::VFXGradientStop stop;
            stop.position = p.w;
            float a = (i < alphas.size()) ? alphas[i] : 1.0f;
            stop.color = glm::vec4(p.x, p.y, p.z, a);
            gradient.stops.push_back(stop);
        }
        std::sort(gradient.stops.begin(), gradient.stops.end(),
                  [](const auto& a, const auto& b) { return a.position < b.position; });
        dirty = false;
        sortedDirty = true;
    }

    void VFXGradientDelegate::rebuildSortedCache()
    {
        sortedCache = points;
        std::sort(sortedCache.begin(), sortedCache.end(),
                  [](const ImVec4& a, const ImVec4& b) { return a.w < b.w; });
        sortedDirty = false;
    }

    int VFXGradientDelegate::EditPoint(int pointIndex, ImVec4 value)
    {
        points[pointIndex] = value;
        dirty = true;
        sortedDirty = true;
        return pointIndex;
    }

    ImVec4 VFXGradientDelegate::GetPoint(float t)
    {
        if (points.empty()) return {1, 1, 1, 0};
        if (points.size() == 1) return points[0];

        if (sortedDirty) rebuildSortedCache();

        if (t <= sortedCache.front().w) return sortedCache.front();
        if (t >= sortedCache.back().w) return sortedCache.back();

        for (size_t i = 0; i < sortedCache.size() - 1; ++i)
        {
            if (t >= sortedCache[i].w && t <= sortedCache[i + 1].w)
            {
                float range = sortedCache[i + 1].w - sortedCache[i].w;
                float frac = (range > 0.0001f) ? (t - sortedCache[i].w) / range : 0.0f;
                return ImVec4(
                    sortedCache[i].x + (sortedCache[i + 1].x - sortedCache[i].x) * frac,
                    sortedCache[i].y + (sortedCache[i + 1].y - sortedCache[i].y) * frac,
                    sortedCache[i].z + (sortedCache[i + 1].z - sortedCache[i].z) * frac,
                    t);
            }
        }
        return sortedCache.back();
    }

    void VFXGradientDelegate::AddPoint(ImVec4 value)
    {
        points.push_back(value);
        alphas.push_back(1.0f);
        dirty = true;
        sortedDirty = true;
    }

    void VFXPropertyPanel::notifyChanged()
    {
        if (onPropertyChanged) onPropertyChanged();
    }

    void VFXPropertyPanel::drawCurveEditor(vfx::VFXCurve& curve, const vfx::VFXProperty& prop)
    {
        const std::string& label = prop.name;
        std::string key = label;
        if (lastPropertyKey != key)
        {
            curveDelegate.syncFrom(curve, prop.min, prop.max);
            lastPropertyKey = key;
        }

        ImGui::Text("%s", label.c_str());
        ImGui::Spacing();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float editorWidth = std::max(avail.x - 10.0f, 200.0f);
        float editorHeight = std::max(avail.y - 30.0f, 100.0f);

        unsigned int curveId = static_cast<unsigned int>(std::hash<std::string>{}(key));
        ImCurveEdit::Edit(curveDelegate, ImVec2(editorWidth, editorHeight), curveId);

        if (curveDelegate.dirty)
        {
            curveDelegate.syncTo(curve);
            notifyChanged();
        }
    }

    void VFXPropertyPanel::drawGradientEditor(vfx::VFXGradient& gradient, const std::string& label)
    {
        std::string key = label;
        if (lastPropertyKey != key)
        {
            gradientDelegate.syncFrom(gradient);
            gradientSelection = -1;
            lastPropertyKey = key;
        }

        ImGui::Text("%s", label.c_str());
        ImGui::Spacing();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float editorWidth = std::max(avail.x - 10.0f, 200.0f);

        ImGradient::Edit(gradientDelegate, ImVec2(editorWidth, 30), gradientSelection);

        if (gradientDelegate.dirty)
        {
            gradientDelegate.syncTo(gradient);
            notifyChanged();
        }

        if (gradientSelection >= 0 && gradientSelection < static_cast<int>(gradient.stops.size()))
        {
            ImGui::Spacing();
            auto& stop = gradient.stops[gradientSelection];
            float color[3] = {stop.color.r, stop.color.g, stop.color.b};
            ImGui::PushItemWidth(200);
            if (ImGui::ColorEdit3("Color", color))
            {
                stop.color.r = color[0];
                stop.color.g = color[1];
                stop.color.b = color[2];
                gradientDelegate.syncFrom(gradient);
                notifyChanged();
            }
            float alpha = stop.color.a;
            if (ImGui::SliderFloat("Alpha", &alpha, 0.0f, 1.0f, "%.2f"))
            {
                stop.color.a = alpha;
                gradientDelegate.syncFrom(gradient);
                notifyChanged();
            }
            ImGui::PopItemWidth();
        }
    }

    void VFXPropertyPanel::drawFlipbookProperties(vfx::VFXNode& node)
    {
        ImGui::Text("Flipbook");
        ImGui::Separator();

        struct FlipbookEntry { const char* key; const char* label; };
        static constexpr FlipbookEntry entries[] = {
            {"flipbookColumns",     "Columns"},
            {"flipbookRows",        "Rows"},
            {"flipbookFrameRate",   "Frame Rate"},
            {"flipbookRandomStart", "Random Start"},
        };

        float inputWidth = 80.0f;

        for (const auto& entry : entries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;

            auto& prop = it->second;
            std::string widgetId = std::string("##panel_") + entry.key;

            if (prop.type == vfx::VFXPropertyType::Int)
            {
                auto* val = std::get_if<int32_t>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    int v = *val;
                    if (ImGui::DragInt(widgetId.c_str(), &v,
                                       1.0f, static_cast<int>(prop.min), static_cast<int>(prop.max)))
                    {
                        *val = v;
                        notifyChanged();
                    }
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Float)
            {
                auto* val = std::get_if<float>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    if (ImGui::DragFloat(widgetId.c_str(), val, 0.1f, prop.min, prop.max, "%.2f"))
                    {
                        notifyChanged();
                    }
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Bool)
            {
                auto* val = std::get_if<bool>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine();
                    if (ImGui::Checkbox(widgetId.c_str(), val))
                    {
                        notifyChanged();
                    }
                }
            }
        }
    }

    void VFXPropertyPanel::drawMeshPathSelector(vfx::VFXNode& node, float inputWidth)
    {
        auto meshIt = node.properties.find("meshPath");
        if (meshIt == node.properties.end()) return;

        auto* meshVal = std::get_if<std::string>(&meshIt->second.value);
        if (!meshVal) return;

        ImGui::Text("Mesh");
        ImGui::SameLine(100.0f);
        std::string display = meshVal->empty() ? "(none)" : std::filesystem::path(*meshVal).filename().string();
        char buf[256];
        std::strncpy(buf, display.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(inputWidth * 1.5f);
        ImGui::InputText("##panel_meshPath", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("...##meshBrowse"))
        {
            nfd::FileDialog dialog;
            std::string path = dialog.openFileDialog({
                {L"VF Mesh", L"*.vfMesh"}
            });
            if (!path.empty())
            {
                *meshVal = path;
                notifyChanged();
            }
        }
        if (!meshVal->empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("X##meshClear"))
            {
                meshVal->clear();
                notifyChanged();
            }
        }
    }

    void VFXPropertyPanel::drawRibbonProperties(vfx::VFXNode& node, float inputWidth)
    {
        struct RibbonEntry { const char* key; const char* label; float step; const char* fmt; };
        static constexpr RibbonEntry ribbonEntries[] = {
            {"ribbonWidth",       "Width",        0.01f, "%.2f"},
            {"ribbonMinDistance",  "Min Distance", 0.01f, "%.2f"},
        };

        auto tpIt = node.properties.find("maxTrailPoints");
        if (tpIt != node.properties.end())
        {
            auto* val = std::get_if<int32_t>(&tpIt->second.value);
            if (val)
            {
                ImGui::Text("Trail Points");
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                if (ImGui::SliderInt("##panel_maxTrailPoints", val, 2, 256))
                {
                    notifyChanged();
                }
            }
        }

        for (const auto& re : ribbonEntries)
        {
            auto it = node.properties.find(re.key);
            if (it == node.properties.end()) continue;
            auto& prop = it->second;
            auto* val = std::get_if<float>(&prop.value);
            if (val)
            {
                ImGui::Text("%s", re.label);
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                std::string wid = std::string("##panel_") + re.key;
                if (ImGui::DragFloat(wid.c_str(), val, re.step, prop.min, prop.max, re.fmt))
                {
                    notifyChanged();
                }
            }
        }
    }

    void VFXPropertyPanel::drawUVScrollProperties(vfx::VFXNode& node, float inputWidth)
    {
        struct UVScrollEntry { const char* key; const char* label; };
        static constexpr UVScrollEntry uvScrollEntries[] = {
            {"uvScrollSpeedU", "UV Scroll U"},
            {"uvScrollSpeedV", "UV Scroll V"},
        };

        for (const auto& entry : uvScrollEntries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;
            auto& prop = it->second;
            auto* val = std::get_if<float>(&prop.value);
            if (val)
            {
                ImGui::Text("%s", entry.label);
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                std::string wid = std::string("##panel_") + entry.key;
                if (ImGui::DragFloat(wid.c_str(), val, 0.01f, prop.min, prop.max, "%.2f"))
                {
                    notifyChanged();
                }
            }
        }
    }

    void VFXPropertyPanel::drawRenderingProperties(vfx::VFXNode& node)
    {
        ImGui::Text("Rendering");
        ImGui::Separator();

        float inputWidth = 80.0f;

        int currentRenderMode = 0;
        {
            auto rmIt = node.properties.find("renderMode");
            if (rmIt != node.properties.end())
            {
                auto& prop = rmIt->second;
                if (auto* val = std::get_if<int32_t>(&prop.value))
                {
                    currentRenderMode = std::clamp(*val, 0, 4);
                    ImGui::Text("Render Mode");
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth * 1.5f);
                    const char* modes[] = {"Billboard", "Stretched", "Horizontal", "Mesh Particle", "Ribbon"};
                    int current = currentRenderMode;
                    if (ImGui::Combo("##panel_renderMode", &current, modes, 5))
                    {
                        *val = current;
                        currentRenderMode = current;
                        notifyChanged();
                    }
                }
            }
        }

        if (currentRenderMode == 3)
            drawMeshPathSelector(node, inputWidth);

        if (currentRenderMode == 4)
            drawRibbonProperties(node, inputWidth);

        drawUVScrollProperties(node, inputWidth);

        struct RenderEntry { const char* key; const char* label; };
        static constexpr RenderEntry entries[] = {
            {"alphaClipThreshold",   "Alpha Clip"},
            {"additiveBlend",        "Additive"},
            {"softParticleDistance",  "Soft Distance"},
            {"stretchMultiplier",    "Stretch"},
        };

        for (const auto& entry : entries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;

            auto& prop = it->second;
            std::string widgetId = std::string("##panel_") + entry.key;

            bool disableWidget = (strcmp(entry.key, "stretchMultiplier") == 0 && currentRenderMode != 1);
            if (disableWidget) ImGui::BeginDisabled();

            if (prop.type == vfx::VFXPropertyType::Float)
            {
                auto* val = std::get_if<float>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    if (ImGui::DragFloat(widgetId.c_str(), val, 0.01f, prop.min, prop.max, "%.2f"))
                    {
                        notifyChanged();
                    }
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Bool)
            {
                auto* val = std::get_if<bool>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine();
                    if (ImGui::Checkbox(widgetId.c_str(), val))
                    {
                        notifyChanged();
                    }
                }
            }

            if (disableWidget) ImGui::EndDisabled();
        }
    }

    void VFXPropertyPanel::draw(vfx::VFXGraph* graph, uint32_t selectedNodeId)
    {
        if (!graph || selectedNodeId == 0)
        {
            ImGui::TextDisabled("Select a node to edit its properties");
            return;
        }

        vfx::VFXNode* node = graph->findNode(selectedNodeId);
        if (!node)
        {
            ImGui::TextDisabled("Select a node to edit its properties");
            return;
        }

        if (lastSelectedNodeId != selectedNodeId)
        {
            lastSelectedNodeId = selectedNodeId;
            lastPropertyKey.clear();
        }

        if (node->type == vfx::VFXNodeType::Emitter)
        {
            drawFlipbookProperties(*node);
            ImGui::Spacing();
            drawRenderingProperties(*node);
            return;
        }

        if (!vfx::isModifierNode(node->type))
        {
            ImGui::TextDisabled("Select a modifier or emitter node to edit properties");
            return;
        }

        ImGui::Text("%s", node->name.c_str());
        ImGui::Separator();

        for (auto& [propName, prop] : node->properties)
        {
            if (prop.type == vfx::VFXPropertyType::Curve)
            {
                auto* curve = std::get_if<vfx::VFXCurve>(&prop.value);
                if (curve)
                {
                    drawCurveEditor(*curve, prop);
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Gradient)
            {
                auto* gradient = std::get_if<vfx::VFXGradient>(&prop.value);
                if (gradient)
                {
                    drawGradientEditor(*gradient, propName);
                }
            }
        }
    }
}
