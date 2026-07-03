#include "VFXPropertyPanel.hpp"
#include "imgui.h"
#include <vfx/VFXEmitterSections.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>

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
            // Core is always shown and cannot be removed; every other section is an
            // opt-in "module" (see VFXEmitterSections.hpp), revealed via "Add Module".
            if (ImGui::CollapsingHeader("Core", ImGuiTreeNodeFlags_DefaultOpen))
                drawCoreProperties(*node);

            drawEmitterSections(*node);
            drawAddModulePopup(*node);

            return;
        }

        if (vfx::isForceNode(node->type))
        {
            drawForceProperties(*node);
            return;
        }

        if (vfx::isShapeNode(node->type))
        {
            drawShapeProperties(*node);
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
