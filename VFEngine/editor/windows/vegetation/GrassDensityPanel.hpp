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
        float brushRadius = 5.0f;
        float brushStrength = 10.0f;
        float brushOpacity = 1.0f;
        int falloffIndex = 2;
        int shapeIndex = 0;

        // Billboard palette
        int selectedVegetationType = 0; // Always Billboard (0)

        // Billboard palette
        std::vector<vegetation::BillboardPaletteEntry> billboardEntries;
        int selectedBillboardIndex = -1; // -1 = All (Random)

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
        void pushGrassConfig();
        void pushBillboardPalette();
    };
}
