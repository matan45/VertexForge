#pragma once

#include "events/EventTypes.hpp"
#include "meshbrush/MeshBrushTypes.hpp"
#include "nfd/FileDialog.hpp"

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
        bool eraseSelectedTypeOnly = false;

        int selectedPaletteIndex = -1; // -1 = All (weighted random)
        bool paletteDirty = false;
        std::vector<meshbrush::MeshPaletteEntry> paletteEntries;
        nfd::FileDialog fileDialog;

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
        void drawPaletteCombo();
        void drawPaletteEntry(int index, int& removeIndex);
        void pushParams();
    };
}
