#pragma once

#include "events/EventTypes.hpp"
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationTypes.hpp"

namespace windows
{
    class GrassDensityPanel
    {
    private:
        bool visible = false;
        int selectedBrushType = 0;
        vegetation::VegetationBrushParams brushParams;
        // UI-only slope mask values (converted to/from brushParams cos limits)
        float slopeMinDeg = 0.0f;
        float slopeMaxDeg = 90.0f;

        // Billboard palette
        std::vector<vegetation::BillboardPaletteEntry> billboardEntries;

        vegetation::GrassRenderConfig grassConfig;
        bool configLoaded = false;

        events::SubscriptionToken modeToken;
        bool subscribed = false;

    public:
        GrassDensityPanel() = default;
        ~GrassDensityPanel();

        void draw();
        void setVisible(bool v) { visible = v; }
        [[nodiscard]] bool isVisible() const { return visible; }

    private:
        void subscribe();
        void ensureConfigLoaded();
        void drawBillboardPalette();
        void drawBillboardEntry(int index, int& removeIndex);
        void drawBrushControls();
        void drawPlacementMaskControls(bool& paramsChanged);
        void pushBrushParams();
        void drawWindControls();
        void drawSSSControls();
        void pushGrassConfig();
        void pushBillboardPalette();
    };
}
