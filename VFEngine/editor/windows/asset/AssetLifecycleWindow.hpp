#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "resource/AssetTypes.hpp"
#include <vector>

namespace windows
{
    class AssetLifecycleWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        int filterType = 0;

        std::vector<resource::AssetEntry> cachedAssets;
        std::vector<resource::AssetEntry> cachedPending;
        float refreshTimer = 0.0f;
        static constexpr float REFRESH_INTERVAL = 0.5f;

        // Memory budget (0 = disabled); edit field in MB, applied on Apply
        size_t budgetBytes = 0;
        size_t trackedBytes = 0;
        bool overBudget = false;
        int budgetEditMb = 0;
        bool budgetEditInitialized = false;

    public:
        explicit AssetLifecycleWindow() = default;
        ~AssetLifecycleWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void refreshData();
        void drawSummary();
        void drawAssetTable();
        void drawPendingQueue();
    };
}
