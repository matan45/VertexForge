#pragma once

#include "events/EventTypes.hpp"
#include "meshbrush/MeshBrushTypes.hpp"

namespace windows
{
    class MeshBrushToolPanel
    {
    private:
        bool visible = false;
        int selectedMode = 0; // 0=Paint, 1=Erase
        float brushRadius = 5.0f;
        float brushDensity = 1.0f;
        float brushSpacing = 2.0f;
        bool continuousMode = true;
        float positionJitter = 0.5f;
        int falloffIndex = 2;

        std::vector<meshbrush::MeshPaletteEntry> paletteEntries;

        events::SubscriptionToken modeToken;
        bool subscribed = false;

    public:
        MeshBrushToolPanel() = default;
        ~MeshBrushToolPanel();

        void draw();
        void setVisible(bool v) { visible = v; }
        [[nodiscard]] bool isVisible() const { return visible; }

    private:
        void subscribe();
        void drawBrushSettings();
        void drawPaletteSection();
        void pushParams();
    };
}
