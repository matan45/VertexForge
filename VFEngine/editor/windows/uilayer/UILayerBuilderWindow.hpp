#pragma once

// VK-1435 — UI Layer Builder window (offscreen WYSIWYG canvas editor).
//
// A UE5/Unity-style editor that renders a UICanvasComponent-rooted sandbox screen offscreen
// to a live ImGui image (exactly as it renders at runtime) and lets the author add / select /
// move / resize / re-anchor elements with on-canvas handles, style with .vfTheme, save as a
// reusable .vfPrefab, and undo/redo edits.
//
// Architecture: the editor stays Graphics-free and talks to the engine ONLY through the CQRS
// EventDispatcher — UIComponentService for element edits, scene events for entity lifecycle /
// selection / hierarchy, ScenePersistence events for .vfPrefab save/load, UndoRedo events, and
// the VK-1435 uilayerpreview events for the offscreen render + reference-extent hit-test /
// resolved-rect. The authored entities live in the one singleton registry (isolated from the
// main scene by the UIPreviewTagComponent the serializer + main UI pass skip); this window
// never touches entt directly. Standalone window class registered as a value member of
// MainImguiWindow, toggled from the Tools menu (clones ThemeEditorWindow's wiring).

#include "nfd/FileDialog.hpp"
#include "data/EntityHandle.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "events/EventDispatcher.hpp"
#include "UILayerCanvasHandles.hpp"
#include "UILayerInspectorUndo.hpp"

// Embed the existing UI inspector drawers for the selection (same set EntityDetailsPanel uses).
#include "../details/UICanvasDrawer.hpp"
#include "../details/UIRectDrawer.hpp"
#include "../details/UIImageDrawer.hpp"
#include "../details/UILabelDrawer.hpp"
#include "../details/UIScrollDrawer.hpp"
#include "../details/UILayoutGroupDrawer.hpp"
#include "../details/UIButtonDrawer.hpp"
#include "../details/UITextInputDrawer.hpp"
#include "../details/UICheckboxDrawer.hpp"
#include "../details/UIDropdownDrawer.hpp"
#include "../details/UITabsDrawer.hpp"
#include "../details/UISliderDrawer.hpp"
#include "../details/UIProgressBarDrawer.hpp"
#include "../details/UIStyleDrawer.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

struct ImDrawList;

namespace windows
{
    class UILayerBuilderWindow
    {
    public:
        UILayerBuilderWindow();
        ~UILayerBuilderWindow();

        void draw();
        void show();

        // Open a UICanvas-rooted .vfPrefab from the content browser: show the window and load
        // the layer into the sandbox (replaces any current layer). Wired to the auto-routing of
        // UI prefabs (PreviewWindowManager -> OpenUILayerBuilderNotification -> MainImguiWindow).
        void openFromContentBrowser(const std::string& path);

    private:
        // ---- Lifecycle / sandbox ------------------------------------------------
        void ensurePreviewInited();          // lazy InitUILayerPreviewCommand (once)
        void newLayer();                     // create a fresh tagged sandbox canvas
        void openLayer(const std::string& path); // LoadPrefab into the sandbox + tag
        void closeLayer();                   // teardown: clean preview + destroy subtree
        void saveLayer(bool saveAs);         // SavePrefab + .vfmeta + AssetSaved
        bool hasLayer() const { return canvasRoot.isValid(); }

        // ---- Preview build / render --------------------------------------------
        void rebuildPreview();               // BuildUILayerPreview at current ref res
        void* renderPreview();               // RenderUILayerPreviewQuery -> descriptor set

        // ---- Panes --------------------------------------------------------------
        void drawToolbar();
        void drawPalettePane();
        void drawHierarchyPane();
        void drawCanvasPane();
        void drawInspectorPane();

        // ---- Canvas interaction (handles + click-select) -----------------------
        void drawCanvasImageAndHandles(glm::vec2 regionOrigin, glm::vec2 regionSize);
        void drawHandleOverlay(ImDrawList* dl, const uilayer::LetterboxMapping& map,
                               const uilayer::RefRect& rect);
        void handleCanvasInput(const uilayer::LetterboxMapping& map);

        // ---- Hierarchy helpers --------------------------------------------------
        void drawHierarchyNode(services::EntityHandle entity, int depth);

        // ---- Palette --------------------------------------------------------------
        enum class WidgetType
        {
            Panel, Label, Button, Image, TextInput, Checkbox,
            Slider, ProgressBar, ScrollView, LayoutGroup, ListView
        };
        void addWidget(WidgetType type);

        // ---- Selection ----------------------------------------------------------
        services::EntityHandle selectedEntity() const;
        void selectEntity(services::EntityHandle entity);
        std::optional<services::UIRectData> rectDataOf(services::EntityHandle entity) const;
        std::optional<uilayer::RefRect> resolvedRectOf(services::EntityHandle entity) const;

        // ---- Undo helpers -------------------------------------------------------
        void pushRectEditUndo(services::EntityHandle entity,
                              const services::UIRectData& before,
                              const services::UIRectData& after,
                              const std::string& description);

        // ---- Reference resolution ----------------------------------------------
        glm::vec2 canvasReferenceExtent() const; // from the sandbox UICanvasComponent
        void applyReferenceResolution();         // push refW/H to canvas + rebuild preview

        // ====================================================================
        bool visible = false;
        nfd::FileDialog fileDialog;

        // Per-window preview instance id (the controller/adapter key). Uses 'this'.
        services::PreviewInstanceId instanceId() const
        {
            return services::PreviewInstanceId(const_cast<UILayerBuilderWindow*>(this));
        }
        bool previewInited = false;

        // The sandbox canvas root (lives in the singleton registry, tagged preview). For a
        // canvas-less UI prefab this is a SYNTHETIC canvas that wraps the loaded fragment.
        services::EntityHandle canvasRoot = services::EntityHandle::invalid();

        // The entity that gets SAVED. Equals canvasRoot for a fresh / UICanvas-rooted layer, or
        // the loaded prefab subtree when a canvas-less fragment was wrapped in a synthetic canvas
        // (so the .vfPrefab round-trips in its original canvas-less form, without the wrapper).
        services::EntityHandle contentRoot = services::EntityHandle::invalid();

        // Persistence state.
        std::string layerPath;   // .vfPrefab path (empty = unsaved/new)
        bool dirty = false;

        // Reference resolution (WYSIWYG). Defaults to the canvas's own referenceWidth/Height.
        int refWidth = 1920;
        int refHeight = 1080;
        int refPreset = 0; // index into the resolution preset list

        // Canvas view state.
        float zoom = 1.0f;
        glm::vec2 panRef{0.0f, 0.0f};
        bool showAnchors = true;
        bool showPivots = false;

        // Drag-edit coalescing: snapshot the rect on mouse-down, push one undo on mouse-up.
        bool dragging = false;
        uilayer::HandleKind activeHandle = uilayer::HandleKind::None;
        services::EntityHandle dragEntity = services::EntityHandle::invalid();
        services::UIRectData dragBefore{};       // rect data at drag start
        uilayer::RefRect dragStartRect{};        // resolved rect at drag start
        glm::vec2 dragStartRefMouse{0.0f, 0.0f}; // mouse (ref px) at drag start

        // Inspector coalescing: snapshot ALL editable UI component data of the selected
        // entity before an edit session begins, push ONE undo entry on edit-deactivation.
        services::EntityHandle inspectorEditEntity = services::EntityHandle::invalid();
        uilayer::UIComponentSnapshot inspectorSnapshotBefore{};
        bool inspectorEditActive = false;

        // Theme picker.
        std::string themePath;

        // Embedded inspector drawers (stateless; same instances EntityDetailsPanel uses).
        details::UICanvasDrawer uiCanvasDrawer;
        details::UIRectDrawer uiRectDrawer;
        details::UIImageDrawer uiImageDrawer;
        details::UILabelDrawer uiLabelDrawer;
        details::UIScrollDrawer uiScrollDrawer;
        details::UILayoutGroupDrawer uiLayoutGroupDrawer;
        details::UIButtonDrawer uiButtonDrawer;
        details::UITextInputDrawer uiTextInputDrawer;
        details::UICheckboxDrawer uiCheckboxDrawer;
        details::UIDropdownDrawer uiDropdownDrawer;
        details::UITabsDrawer uiTabsDrawer;
        details::UISliderDrawer uiSliderDrawer;
        details::UIProgressBarDrawer uiProgressBarDrawer;
        details::UIStyleDrawer uiStyleDrawer;
    };
}
