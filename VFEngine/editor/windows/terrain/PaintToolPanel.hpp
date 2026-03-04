#pragma once

#include "events/EventTypes.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include <memory>
#include <string>

namespace windows
{
    class PaintToolPanel
    {
    private:
        bool visible = false;
        int selectedBrushType = 0;
        float brushRadius = 5.0f;
        float brushStrength = 10.0f;
        float brushOpacity = 1.0f;
        int activeLayer = 0;
        int falloffIndex = 2;
        int shapeIndex = 0;

        // Cached terrain material data for layer names
        std::shared_ptr<terrain::TerrainMaterialData> materialData;
        std::string materialPath;

        events::SubscriptionToken paintModeToken;
        events::SubscriptionToken brushTypeToken;
        events::SubscriptionToken brushParamsToken;

        bool subscribed = false;

    public:
        PaintToolPanel() = default;
        ~PaintToolPanel();

        void draw();

    private:
        void subscribe();
        void loadMaterialFromTarget();
    };
}
