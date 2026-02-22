#include "VFXPropertyPanel.hpp"
#include "imgui.h"
#include <algorithm>

namespace editor::vfxeditor
{
    // --- VFXCurveDelegate ---

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

        for (size_t i = 0; i < curve.keys.size(); ++i)
        {
            if (curve.keys.size() <= 1)
            {
                curve.keys[i].inTangent = 0.0f;
                curve.keys[i].outTangent = 0.0f;
            }
            else if (i == 0)
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

    // --- VFXGradientDelegate ---

    void VFXGradientDelegate::syncFrom(const vfx::VFXGradient& gradient)
    {
        points.clear();
        for (const auto& stop : gradient.stops)
        {
            points.push_back({stop.color.r, stop.color.g, stop.color.b, stop.position});
        }
        dirty = false;
    }

    void VFXGradientDelegate::syncTo(vfx::VFXGradient& gradient)
    {
        gradient.stops.clear();
        for (const auto& p : points)
        {
            vfx::VFXGradientStop stop;
            stop.position = p.w;
            stop.color = glm::vec4(p.x, p.y, p.z, 1.0f);
            gradient.stops.push_back(stop);
        }
        std::sort(gradient.stops.begin(), gradient.stops.end(),
                  [](const auto& a, const auto& b) { return a.position < b.position; });
        dirty = false;
    }

    int VFXGradientDelegate::EditPoint(int pointIndex, ImVec4 value)
    {
        points[pointIndex] = value;
        dirty = true;
        return pointIndex;
    }

    ImVec4 VFXGradientDelegate::GetPoint(float t)
    {
        if (points.empty()) return {1, 1, 1, 0};
        if (points.size() == 1) return points[0];

        std::vector<ImVec4> sorted = points;
        std::sort(sorted.begin(), sorted.end(),
                  [](const ImVec4& a, const ImVec4& b) { return a.w < b.w; });

        if (t <= sorted.front().w) return sorted.front();
        if (t >= sorted.back().w) return sorted.back();

        for (size_t i = 0; i < sorted.size() - 1; ++i)
        {
            if (t >= sorted[i].w && t <= sorted[i + 1].w)
            {
                float range = sorted[i + 1].w - sorted[i].w;
                float frac = (range > 0.0001f) ? (t - sorted[i].w) / range : 0.0f;
                return ImVec4(
                    sorted[i].x + (sorted[i + 1].x - sorted[i].x) * frac,
                    sorted[i].y + (sorted[i + 1].y - sorted[i].y) * frac,
                    sorted[i].z + (sorted[i + 1].z - sorted[i].z) * frac,
                    t);
            }
        }
        return sorted.back();
    }

    void VFXGradientDelegate::AddPoint(ImVec4 value)
    {
        points.push_back(value);
        dirty = true;
    }

    // --- VFXPropertyPanel ---

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
                    ImGui::SameLine(100.0f);
                    if (ImGui::Checkbox(widgetId.c_str(), val))
                    {
                        notifyChanged();
                    }
                }
            }
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

        // Emitter node: show flipbook properties
        if (node->type == vfx::VFXNodeType::Emitter)
        {
            drawFlipbookProperties(*node);
            return;
        }

        // Modifier nodes: show curve/gradient editors
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
