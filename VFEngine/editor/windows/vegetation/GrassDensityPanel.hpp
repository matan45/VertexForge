#pragma once

#include "events/EventTypes.hpp"
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationTypes.hpp"
#include "vegetation/VegetationScatterTypes.hpp"

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

        // Scatter rules (VK-1581)
        vegetation::ScatterProfile scatterProfile;
        int scatterSeed = 1337;
        int lastPlacedCount = -1;   // <0 = no bake yet this session
        int lastTotalCount = 0;
        bool lastBudgetExceeded = false;

        events::SubscriptionToken modeToken;
        events::SubscriptionToken scatterToken;
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
        void drawScatterControls();
        void drawScatterRule(int index, int& removeIndex, bool& changed);
        void pushScatterProfile();
        void generateScatter(bool replaceProcedural);
    };
}
