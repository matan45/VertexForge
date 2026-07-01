#pragma once

#include <vfx/VFXTypes.hpp>
#include <vfx/VFXCurveTypes.hpp>
#include "ImCurveEdit.h"
#include "ImGradient.h"
#include <functional>
#include <vector>
#include <string>

namespace editor::vfxeditor
{
    using PropertyChangedCallback = std::function<void()>;

    class VFXCurveDelegate : public ImCurveEdit::Delegate
    {
        std::vector<ImVec2> points;
        ImVec2 rangeMin{0.0f, 0.0f};
        ImVec2 rangeMax{1.0f, 1.0f};

    public:
        bool dirty = false;

        void syncFrom(const vfx::VFXCurve& curve, float yMin = 0.0f, float yMax = 1.0f);
        void syncTo(vfx::VFXCurve& curve);

        size_t GetCurveCount() override { return 1; }
        bool IsVisible(size_t) override { return true; }
        ImCurveEdit::CurveType GetCurveType(size_t) const override { return ImCurveEdit::CurveSmooth; }
        ImVec2& GetMin() override { return rangeMin; }
        ImVec2& GetMax() override { return rangeMax; }
        size_t GetPointCount(size_t) override { return points.size(); }
        uint32_t GetCurveColor(size_t) override { return 0xFF40FF40; }
        ImVec2* GetPoints(size_t) override { return points.data(); }
        int EditPoint(size_t, int pointIndex, ImVec2 value) override;
        void AddPoint(size_t, ImVec2 value) override;
    };

    class VFXGradientDelegate : public ImGradient::Delegate
    {
        std::vector<ImVec4> points;
        std::vector<float> alphas; // Per-stop alpha (separate from ImVec4 which uses .w for position)
        std::vector<ImVec4> sortedCache;
        bool sortedDirty = true;

        void rebuildSortedCache();

    public:
        bool dirty = false;

        void syncFrom(const vfx::VFXGradient& gradient);
        void syncTo(vfx::VFXGradient& gradient);

        size_t GetPointCount() override { return points.size(); }
        ImVec4* GetPoints() override { return points.data(); }
        int EditPoint(int pointIndex, ImVec4 value) override;
        ImVec4 GetPoint(float t) override;
        void AddPoint(ImVec4 value) override;
    };

    class VFXPropertyPanel
    {
    private:
        PropertyChangedCallback onPropertyChanged;

        VFXCurveDelegate curveDelegate;
        VFXGradientDelegate gradientDelegate;
        int gradientSelection = -1;

        uint32_t lastSelectedNodeId = 0;
        std::string lastPropertyKey;

    public:
        void draw(vfx::VFXGraph* graph, uint32_t selectedNodeId);
        void setOnPropertyChanged(PropertyChangedCallback callback) { onPropertyChanged = callback; }

    private:
        void drawCurveEditor(vfx::VFXCurve& curve, const vfx::VFXProperty& prop);
        void drawGradientEditor(vfx::VFXGradient& gradient, const std::string& label);
        bool drawScalarProperty(const char* label, vfx::VFXProperty& prop, float inputWidth, float step = 0.1f);
        void drawCoreProperties(vfx::VFXNode& node);
        void drawForceProperties(vfx::VFXNode& node);
        void drawShapeProperties(vfx::VFXNode& node);
        void drawFlipbookProperties(vfx::VFXNode& node);
        void drawRenderingProperties(vfx::VFXNode& node);
        void drawMeshPathSelector(vfx::VFXNode& node, float inputWidth);
        void drawRibbonProperties(vfx::VFXNode& node, float inputWidth);
        void drawUVScrollProperties(vfx::VFXNode& node, float inputWidth);
        void drawBurstProperties(vfx::VFXNode& node, float inputWidth);
        void drawEventsProperties(vfx::VFXNode& node, float inputWidth);
        void drawLightingProperties(vfx::VFXNode& node);
        void drawCollisionProperties(vfx::VFXNode& node, float inputWidth);
        void drawDistortionProperties(vfx::VFXNode& node);
        void notifyChanged();
    };
}
