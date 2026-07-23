#pragma once
// VK-1575 (Phase 5): editor UI for the foliage brush. Mirrors
// meshbrush/MeshBrushToolPanel: subscribes to the mode-changed notification,
// draws the mode/brush/palette widgets and drives the foliage brush + palette
// through the CQRS dispatcher. The FoliageType palette lives on TerrainService;
// this panel syncs FROM it on activate and pushes back only on user edits.
#include "events/EventTypes.hpp"
#include "foliage/FoliageBrushTypes.hpp"
#include "foliage/FoliageTypes.hpp"
#include "nfd/FileDialog.hpp"
#include <vector>
#include <cstdint>

namespace windows
{
    class FoliageBrushToolPanel
    {
    private:
        bool visible = false;
        int selectedMode = 0; // 0=Paint, 1=Erase

        // Brush geometry (defaults mirror foliage::FoliageBrushParams).
        float brushRadius = 5.0f;
        float brushDensity = 1.0f;
        float brushSpacing = 2.0f;
        float positionJitter = 0.5f;
        int falloffIndex = 2;        // terrain::BrushFalloff::Smooth
        int placementModeIndex = 0;  // 0=Spray, 1=Single
        float flowRate = 8.0f;
        float tintColor[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // -> packed 0xRRGGBBAA (0xFFFFFFFF = none)
        bool eraseSelectedTypeOnly = false;

        int selectedPaletteIndex = -1; // -1 = All (weighted random)
        bool paletteDirty = false;
        std::vector<foliage::FoliageType> paletteEntries;
        nfd::FileDialog fileDialog;

        events::SubscriptionToken modeToken;
        bool subscribed = false;

    public:
        FoliageBrushToolPanel() = default;
        ~FoliageBrushToolPanel();

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
        void pushPalette();
        [[nodiscard]] uint32_t packTint() const;
    };
}
