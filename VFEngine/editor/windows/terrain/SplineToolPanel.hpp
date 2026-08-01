#pragma once

#include "events/EventTypes.hpp"
#include "terrain/SplineTypes.hpp"
#include "nfd/FileDialog.hpp"
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>

namespace windows
{
    class SplineToolPanel
    {
    private:
        bool visible = false;

        // Single source of truth for everything the service knows about. VK-1616's lesson
        // (SculptToolPanel.hpp:63-67): the old panel mirrored each field into its own member and
        // hand-maintained two copies of the list — the mode-enter pull and the params push — which
        // is exactly how a control ends up silently not being wired. Both directions funnel through
        // applyParams()/pushParams() over this one struct.
        terrain::SplineParams params;

        // Editing state for widgets that cannot bind to `params` directly.
        int paintLayer = 1;
        float roadHalfWidth = 4.0f;
        float roadShoulderWidth = 1.5f;
        float roadShoulderDrop = 0.15f;
        bool roadProfileIsCustom = false; // authored columns the three scalars cannot describe
        std::array<char, 64> roadNameBuffer{};

        uint32_t pointCount = 0;

        // Preview cache. The revision is what makes the cache correct once control points can
        // move: keying on (pointCount, corridorWidth) alone missed every edit that kept the count.
        uint32_t previewRevision = 0;
        uint32_t cachedPreviewRevision = 0xFFFFFFFFu;
        std::vector<glm::vec3> cachedSamples;

        events::SubscriptionToken modeToken;
        events::SubscriptionToken pointToken;
        events::SubscriptionToken paramsToken;

        bool subscribed = false;

        nfd::FileDialog fileDialog;

    public:
        SplineToolPanel() = default;
        ~SplineToolPanel();

        void draw();

    private:
        void subscribe();

        // Pull: service -> panel. Refreshes `params` and every derived widget value.
        void applyParams(const terrain::SplineParams& incoming);

        // Push: panel -> service.
        void pushParams();

        // Rebuilds params.road.columns from the three shoulder scalars.
        void rebuildRoadColumns();

        // Recovers the three scalars from an existing column list. False when the columns were
        // authored into a shape the scalars cannot represent, in which case they are left alone
        // and the panel says so rather than silently overwriting the profile.
        bool deriveRoadScalars(const terrain::RoadProfile& profile);

        void drawOperations(bool& changed);
        void drawCorridorParams(bool& changed);
        void drawRoadParams(bool& changed);
        void drawPointList();
        void drawRoadList();
        void drawPreview();

        // Name of the road currently open for editing; empty when authoring a new spline. Purely
        // an indicator — the service owns the actual replace-on-apply link.
        std::string editingRoadName;
    };
}
