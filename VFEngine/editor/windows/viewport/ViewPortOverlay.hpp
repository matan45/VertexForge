#pragma once
#include "ViewPortGizmo.hpp"
#include "data/DTOs.hpp"
#include <glm/glm.hpp>
#include <utility>

namespace windows
{
    enum class ViewportIcon : uint32_t
    {
        Grid = 0,
        World = 1,
        Rotate = 2,
        Scale = 3,
        Translate = 4,
        Sculpt = 5,
        Paint = 6,
        Hole = 7,
        Vegetation = 8,
        MeshBrush = 9
    };

    class ViewPortOverlay
    {
    private:
        services::EditorTextureHandle iconAtlas;
        bool iconsLoaded = false;
        int currentViewMode = 0; 
        static constexpr uint32_t ATLAS_COLUMNS = 4;
        static constexpr uint32_t ATLAS_ROWS = 4;
        static constexpr float ICON_SIZE = 32.0f;

    public:
        ~ViewPortOverlay();

        void draw(ViewPortGizmo& gizmo);

    private:
        void drawToolbar(ViewPortGizmo& gizmo, ImGuiWindowFlags overlayFlags, const ImVec2& overlayPos);
        void drawViewModeDropdown(ImGuiWindowFlags overlayFlags, const ImVec2& windowPos,
                                  const ImVec2& contentMin);
        bool isTerrainSelected() const;

        void loadIconAtlas();
        std::pair<glm::vec2, glm::vec2> getIconUV(ViewportIcon icon) const;
        bool iconButton(ViewportIcon icon, bool isActive, const char* tooltip);
    };
}
