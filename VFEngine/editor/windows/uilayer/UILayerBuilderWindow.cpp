#include "UILayerBuilderWindow.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/ui/UICanvasRectImageEvents.hpp"
#include "events/ui/UILabelButtonEvents.hpp"
#include "events/ui/UIInputCheckboxEvents.hpp"
#include "events/ui/UIScrollLayoutEvents.hpp"
#include "events/ui/UISliderProgressEvents.hpp"
#include "events/ui/UIListViewEvents.hpp"
#include "events/ui/UIThemeEvents.hpp"
#include "events/ui/UIEvents.hpp" // aggregator: all HasUI*ComponentQuery for the Add-Component presence
#include "events/render/UILayerPreviewEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ResourceEvents.hpp"

// VK-1442 — compound-widget structure validation reads the live registry directly (the editor
// may; mutations still flow through CQRS). Conversion + the header-only validator come from the
// utilities include root.
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include "ui/UICompoundValidation.hpp"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace windows
{
    namespace
    {
        using Dispatcher = events::EventDispatcher;

        // WYSIWYG reference-resolution presets.
        struct ResPreset { const char* name; int w; int h; };
        const ResPreset kResPresets[] = {
            {"1920 x 1080 (FHD 16:9)", 1920, 1080},
            {"2560 x 1440 (QHD 16:9)", 2560, 1440},
            {"3840 x 2160 (4K 16:9)", 3840, 2160},
            {"1280 x 720 (HD 16:9)", 1280, 720},
            {"1080 x 1920 (Portrait)", 1080, 1920},
            {"1024 x 768 (4:3)", 1024, 768},
            {"Custom", 0, 0},
        };
        constexpr int kCustomPresetIndex = 6;

        // A re-issuable undo command for a single UIRect edit: stores before/after
        // UIRectData and replays SetUIRectDataCommand on execute()/undo(). The entity
        // handle is captured by value — the sandbox entity is stable for the window's
        // lifetime (deleted only on close, after the undo stack scope ends).
        class UIRectEditUndoCommand : public services::IUndoableCommand
        {
        public:
            UIRectEditUndoCommand(services::EntityHandle entity,
                                  services::UIRectData before, services::UIRectData after,
                                  std::string desc)
                : entity(entity), before(before), after(after), description(std::move(desc))
            {
            }

            void execute() override { apply(after); } // redo
            void undo() override { apply(before); }
            std::string getDescription() const override { return description; }

        private:
            void apply(const services::UIRectData& data)
            {
                events::ui::SetUIRectDataCommand cmd;
                cmd.entity = entity;
                cmd.rectData = data;
                Dispatcher::instance().execute(cmd);
            }

            services::EntityHandle entity;
            services::UIRectData before;
            services::UIRectData after;
            std::string description;
        };
    }

    UILayerBuilderWindow::UILayerBuilderWindow()
        : hierarchyPane(*this), canvasPane(*this)
    {
    }

    UILayerBuilderWindow::~UILayerBuilderWindow()
    {
        // Intentionally NOT issuing CQRS teardown here. This window is a value member of
        // MainImguiWindow, so its destructor runs during editor shutdown — after the scene
        // registry / EventDispatcher may already be gone — and dispatching DeleteEntity /
        // CleanUpUILayerPreview then would be unsafe. The preview controller is owned by the
        // Core adapter (cleared on EditorBootstrap teardown) and the sandbox entity dies with
        // the registry, so leaving them is harmless. In-session teardown is explicit: closing
        // the window (title-bar X) and New/Open both call closeLayer() while the engine lives.
    }

    void UILayerBuilderWindow::show()
    {
        visible = true;
        sizeSaved = false; // re-arm the on-close size persistence for this open session
    }

    void UILayerBuilderWindow::openFromContentBrowser(const std::string& path)
    {
        show();
        openLayer(path);
    }

    // =========================================================================
    // Lifecycle / sandbox
    // =========================================================================

    void UILayerBuilderWindow::ensurePreviewInited()
    {
        if (previewInited) return;
        services::events::uilayerpreview::InitUILayerPreviewCommand cmd;
        cmd.instanceId = instanceId();
        Dispatcher::instance().execute(cmd);
        previewInited = true;
    }

    void UILayerBuilderWindow::newLayer()
    {
        closeLayer();
        ensurePreviewInited();

        // Create a fresh entity, tag it as a preview sandbox (so it never renders in the
        // main viewport nor serializes into the scene), then make it a UICanvas + UIRect.
        events::scene::CreateEntityCommand createCmd;
        createCmd.name = "UI Layer (Builder)";
        canvasRoot = Dispatcher::instance().execute(createCmd);
        if (!canvasRoot.isValid()) return;
        contentRoot = canvasRoot; // a fresh layer is saved from its own canvas root

        events::ui::MarkUIPreviewSandboxCommand markCmd;
        markCmd.entity = canvasRoot;
        markCmd.tagged = true;
        Dispatcher::instance().execute(markCmd);

        events::ui::AddUICanvasComponentCommand canvasCmd;
        canvasCmd.entity = canvasRoot;
        Dispatcher::instance().execute(canvasCmd);

        events::ui::AddUIRectComponentCommand rectCmd;
        rectCmd.entity = canvasRoot;
        Dispatcher::instance().execute(rectCmd);

        // Seed the canvas reference resolution from the window's current selection.
        services::UICanvasData canvasData;
        canvasData.referenceWidth = static_cast<float>(refWidth);
        canvasData.referenceHeight = static_cast<float>(refHeight);
        canvasData.scaleMode = 1; // ScaleWithScreenSize == WYSIWYG-friendly
        events::ui::SetUICanvasDataCommand setCanvas;
        setCanvas.entity = canvasRoot;
        setCanvas.canvasData = canvasData;
        Dispatcher::instance().execute(setCanvas);

        layerPath.clear();
        themePath.clear();
        dirty = false;
        rebuildPreview();
        selectEntity(canvasRoot);
    }

    void UILayerBuilderWindow::openLayer(const std::string& path)
    {
        closeLayer();
        ensurePreviewInited();

        // LoadPrefab into the scene (parent = root).
        events::scene::LoadPrefabCommand loadCmd;
        loadCmd.filePath = path;
        auto loaded = Dispatcher::instance().execute(loadCmd);
        if (!loaded.has_value() || !loaded->isValid())
        {
            return;
        }

        // Is the loaded prefab UICanvas-rooted, or a canvas-less UI fragment (a panel/widget
        // subtree, as HUD prefabs typically are)? The preview controller requires a UICanvas
        // root, so a canvas-less prefab is WRAPPED in a synthetic sandbox canvas that supplies
        // the layout context — exactly what the game's screen canvas provides at runtime.
        events::ui::GetUICanvasDataQuery canvasQuery;
        canvasQuery.entity = *loaded;
        auto loadedCanvas = Dispatcher::instance().query(canvasQuery);

        if (loadedCanvas.has_value())
        {
            // Canvas-rooted: the loaded root is both the sandbox canvas and the saved content.
            canvasRoot = *loaded;
            contentRoot = *loaded;

            // Adopt the canvas's own reference resolution for the WYSIWYG extent.
            refWidth = static_cast<int>(loadedCanvas->referenceWidth);
            refHeight = static_cast<int>(loadedCanvas->referenceHeight);
            refPreset = kCustomPresetIndex;
            for (int i = 0; i < kCustomPresetIndex; ++i)
            {
                if (kResPresets[i].w == refWidth && kResPresets[i].h == refHeight) { refPreset = i; break; }
            }
        }
        else
        {
            // Canvas-less fragment: build a synthetic sandbox canvas and reparent the loaded
            // subtree under it. We SAVE the loaded subtree (contentRoot), not the wrapper, so the
            // .vfPrefab round-trips in its original canvas-less form. The synthetic canvas adopts
            // the window's current reference resolution (the fragment's own size is unknown).
            events::scene::CreateEntityCommand createCmd;
            createCmd.name = "UI Canvas (Builder)";
            canvasRoot = Dispatcher::instance().execute(createCmd);
            if (!canvasRoot.isValid())
            {
                contentRoot = services::EntityHandle::invalid();
                return;
            }

            events::ui::AddUICanvasComponentCommand canvasCmd;
            canvasCmd.entity = canvasRoot;
            Dispatcher::instance().execute(canvasCmd);

            events::ui::AddUIRectComponentCommand rectCmd;
            rectCmd.entity = canvasRoot;
            Dispatcher::instance().execute(rectCmd);

            services::UICanvasData canvasData;
            canvasData.referenceWidth = static_cast<float>(refWidth);
            canvasData.referenceHeight = static_cast<float>(refHeight);
            canvasData.scaleMode = 1; // ScaleWithScreenSize == WYSIWYG-friendly
            events::ui::SetUICanvasDataCommand setCanvas;
            setCanvas.entity = canvasRoot;
            setCanvas.canvasData = canvasData;
            Dispatcher::instance().execute(setCanvas);

            events::scene::ReparentEntityCommand reparent;
            reparent.entity = *loaded;
            reparent.newParent = canvasRoot;
            Dispatcher::instance().execute(reparent);

            contentRoot = *loaded;
        }

        // Tag the sandbox canvas root so the whole subtree is isolated (skipped by the main UI
        // passes + the scene serializer); the saved contentRoot subtree carries no tag.
        events::ui::MarkUIPreviewSandboxCommand markCmd;
        markCmd.entity = canvasRoot;
        markCmd.tagged = true;
        Dispatcher::instance().execute(markCmd);

        layerPath = path;
        dirty = false;
        rebuildPreview();
        selectEntity(contentRoot);
    }

    void UILayerBuilderWindow::closeLayer()
    {
        if (previewInited)
        {
            services::events::uilayerpreview::CleanUpUILayerPreviewCommand cmd;
            cmd.instanceId = instanceId();
            Dispatcher::instance().execute(cmd);
            previewInited = false;
        }

        if (canvasRoot.isValid())
        {
            // Destroy the whole tagged sandbox subtree. No untag needed — it's gone.
            events::scene::DeleteEntityCommand del;
            del.entity = canvasRoot;
            Dispatcher::instance().execute(del);
            canvasRoot = services::EntityHandle::invalid();
        }
        contentRoot = services::EntityHandle::invalid();
        dragging = false;
        dragEntity = services::EntityHandle::invalid();

        // Reset per-layer hierarchy UI state so a recycled entity id can't inherit stale
        // open/seen state, and rename/reveal don't dangle across New/Open.
        renamingEntity = services::EntityHandle::invalid();
        renameFocusPending = false;
        expandedNodes.clear();
        seenNodes.clear();
        lastRevealSel = services::EntityHandle::invalid();
        revealScroll = false;
    }

    void UILayerBuilderWindow::saveLayer(bool saveAs)
    {
        if (!hasLayer()) return;

        std::string path = layerPath;
        if (saveAs || path.empty())
        {
            path = fileDialog.saveFileDialog({{L"VF Prefab (*.vfPrefab)", L"*.vfPrefab"}}, L"vfPrefab");
            if (path.empty()) return;
        }

        // Save the content subtree — for a wrapped canvas-less fragment this is the loaded prefab
        // root, NOT the synthetic wrapper canvas, so the .vfPrefab round-trips in its original form.
        events::scene::SavePrefabCommand cmd;
        cmd.entity = contentRoot.isValid() ? contentRoot : canvasRoot;
        cmd.filePath = path;
        if (Dispatcher::instance().execute(cmd))
        {
            layerPath = path;
            dirty = false;

            events::resource::AssetSavedNotification notif;
            notif.filePath = path;
            Dispatcher::instance().publish(notif);
        }
    }

    // =========================================================================
    // Preview build / render
    // =========================================================================

    void UILayerBuilderWindow::rebuildPreview()
    {
        if (!hasLayer()) return;
        ensurePreviewInited();
        services::events::uilayerpreview::BuildUILayerPreviewCommand cmd;
        cmd.instanceId = instanceId();
        cmd.canvasRoot = canvasRoot;
        cmd.refWidth = static_cast<uint32_t>(std::max(1, refWidth));
        cmd.refHeight = static_cast<uint32_t>(std::max(1, refHeight));
        Dispatcher::instance().execute(cmd);
    }

    void* UILayerBuilderWindow::renderPreview()
    {
        if (!hasLayer()) return nullptr;
        services::events::uilayerpreview::RenderUILayerPreviewQuery query;
        query.instanceId = instanceId();
        auto handle = Dispatcher::instance().query(query);
        return handle.imguiDescriptorSet;
    }

    // =========================================================================
    // Selection
    // =========================================================================

    services::EntityHandle UILayerBuilderWindow::selectedEntity() const
    {
        events::scene::GetSelectedEntityQuery q;
        auto sel = Dispatcher::instance().query(q);
        return sel.value_or(services::EntityHandle::invalid());
    }

    void UILayerBuilderWindow::selectEntity(services::EntityHandle entity)
    {
        events::scene::SelectEntityCommand cmd;
        if (entity.isValid()) cmd.entity = entity;
        Dispatcher::instance().execute(cmd);
    }

    std::optional<services::UIRectData> UILayerBuilderWindow::rectDataOf(services::EntityHandle entity) const
    {
        if (!entity.isValid()) return std::nullopt;
        events::ui::GetUIRectDataQuery q;
        q.entity = entity;
        return Dispatcher::instance().query(q);
    }

    std::optional<uilayer::RefRect> UILayerBuilderWindow::resolvedRectOf(services::EntityHandle entity) const
    {
        if (!entity.isValid()) return std::nullopt;
        services::events::uilayerpreview::GetUILayerResolvedRectQuery q;
        q.instanceId = instanceId();
        q.entity = entity;
        auto r = Dispatcher::instance().query(q);
        if (!r.has_value()) return std::nullopt;
        return uilayer::RefRect{r->x, r->y, r->w, r->h};
    }

    bool UILayerBuilderWindow::isLayoutControlled(services::EntityHandle entity) const
    {
        if (!entity.isValid()) return false;
        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = entity;
        auto data = Dispatcher::instance().query(entityQuery);
        if (!data.has_value() || !data->parent.has_value() || !data->parent->isValid())
            return false;
        events::ui::HasUILayoutGroupComponentQuery layoutQuery;
        layoutQuery.entity = *data->parent;
        return Dispatcher::instance().query(layoutQuery);
    }

    glm::vec2 UILayerBuilderWindow::canvasReferenceExtent() const
    {
        if (canvasRoot.isValid())
        {
            events::ui::GetUICanvasDataQuery q;
            q.entity = canvasRoot;
            if (auto data = Dispatcher::instance().query(q))
            {
                return glm::vec2(data->referenceWidth, data->referenceHeight);
            }
        }
        return glm::vec2(static_cast<float>(refWidth), static_cast<float>(refHeight));
    }

    // =========================================================================
    // Undo
    // =========================================================================

    void UILayerBuilderWindow::pushRectEditUndo(services::EntityHandle entity,
                                                const services::UIRectData& before,
                                                const services::UIRectData& after,
                                                const std::string& description)
    {
        auto cmd = std::make_shared<UIRectEditUndoCommand>(entity, before, after, description);
        events::undoredo::PushUndoableCommand push;
        push.command = cmd;
        Dispatcher::instance().execute(push);
        dirty = true;
    }

    // =========================================================================
    // Palette
    // =========================================================================

    void UILayerBuilderWindow::addWidget(WidgetType type)
    {
        if (!hasLayer()) return;

        // Parent = current selection if any, else the content root (so a widget added with
        // nothing selected lands inside the SAVED subtree, not on the synthetic wrapper canvas).
        services::EntityHandle parent = selectedEntity();
        if (!parent.isValid()) parent = contentRoot;

        const char* name = "UI Element";
        switch (type)
        {
        case WidgetType::Panel:       name = "Panel"; break;
        case WidgetType::Label:       name = "Label"; break;
        case WidgetType::Button:      name = "Button"; break;
        case WidgetType::Image:       name = "Image"; break;
        case WidgetType::TextInput:   name = "TextInput"; break;
        case WidgetType::Checkbox:    name = "Checkbox"; break;
        case WidgetType::Slider:      name = "Slider"; break;
        case WidgetType::ProgressBar: name = "ProgressBar"; break;
        case WidgetType::ScrollView:  name = "ScrollView"; break;
        case WidgetType::LayoutGroup: name = "LayoutGroup"; break;
        case WidgetType::ListView:    name = "ListView"; break;
        case WidgetType::Dropdown:    name = "Dropdown"; break;
        case WidgetType::Tabs:        name = "Tabs"; break;
        case WidgetType::Tooltip:     name = "Tooltip"; break;
        }

        // One undo entry for the whole add gesture.
        events::undoredo::BeginBatchCommand begin;
        begin.description = std::string("Add ") + name;
        Dispatcher::instance().execute(begin);

        events::scene::CreateEntityCommand createCmd;
        createCmd.name = name;
        createCmd.parent = parent;
        services::EntityHandle created = Dispatcher::instance().execute(createCmd);
        if (created.isValid())
        {
            // Every UI element needs a rect. New children inherit the parent's preview tag
            // through the registry hierarchy (the serializer/main-pass skip is checked at the
            // tagged canvas root), so no per-child tagging is required.
            events::ui::AddUIRectComponentCommand rectCmd;
            rectCmd.entity = created;
            Dispatcher::instance().execute(rectCmd);

            // Seed a sensible default rect: a centered 200x60 box anchored at the center.
            services::UIRectData rect;
            rect.anchorMin = glm::vec2(0.5f, 0.5f);
            rect.anchorMax = glm::vec2(0.5f, 0.5f);
            rect.pivot = glm::vec2(0.5f, 0.5f);
            rect.sizeDelta = glm::vec2(200.0f, 60.0f);
            rect.anchoredPosition = glm::vec2(0.0f, 0.0f);
            events::ui::SetUIRectDataCommand setRect;
            setRect.entity = created;
            setRect.rectData = rect;
            Dispatcher::instance().execute(setRect);

            // Add the type-specific component.
            switch (type)
            {
            case WidgetType::Panel:
            {
                events::ui::AddUIImageComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Image:
            {
                events::ui::AddUIImageComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Label:
            {
                events::ui::AddUILabelComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Button:
            {
                events::ui::AddUIButtonComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::TextInput:
            {
                events::ui::AddUITextInputComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Checkbox:
            {
                events::ui::AddUICheckboxComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Slider:
            {
                events::ui::AddUISliderComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::ProgressBar:
            {
                events::ui::AddUIProgressBarComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::ScrollView:
            {
                events::ui::AddUIScrollComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::LayoutGroup:
            {
                events::ui::AddUILayoutGroupComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::ListView:
            {
                events::ui::AddUIListViewComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Dropdown:
            {
                events::ui::AddUIDropdownComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Tabs:
            {
                events::ui::AddUITabsComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            case WidgetType::Tooltip:
            {
                events::ui::AddUITooltipComponentCommand c; c.entity = created;
                Dispatcher::instance().execute(c);
                break;
            }
            }

            // VK-1442 — build the compound widget's starter child composition (no-op for the
            // simple widget types), still inside the undo batch so the whole add is one gesture.
            assembleCompound(type, created);

            selectEntity(created);
            dirty = true;
        }

        events::undoredo::EndBatchCommand end;
        Dispatcher::instance().execute(end);

        rebuildPreview();
    }

    // =========================================================================
    // Compound auto-assembly (VK-1442)
    // =========================================================================

    void UILayerBuilderWindow::assembleCompound(WidgetType type, services::EntityHandle root)
    {
        if (!root.isValid()) return;
        auto& d = Dispatcher::instance();

        // Small CQRS builders (mirror addWidget's create + seed-rect pattern). Each child inherits
        // the sandbox preview tag through the registry hierarchy, so no per-child tagging is needed.
        auto createChild = [&](services::EntityHandle parent, const char* childName) -> services::EntityHandle
        {
            events::scene::CreateEntityCommand c;
            c.name = childName;
            c.parent = parent;
            services::EntityHandle h = d.execute(c);
            if (h.isValid())
            {
                events::ui::AddUIRectComponentCommand rc; rc.entity = h;
                d.execute(rc);
            }
            return h;
        };
        auto setRect = [&](services::EntityHandle e, glm::vec2 aMin, glm::vec2 aMax,
                           glm::vec2 pivot, glm::vec2 size, glm::vec2 pos)
        {
            services::UIRectData r;
            r.anchorMin = aMin; r.anchorMax = aMax; r.pivot = pivot;
            r.sizeDelta = size; r.anchoredPosition = pos;
            events::ui::SetUIRectDataCommand s; s.entity = e; s.rectData = r;
            d.execute(s);
        };
        auto addImage = [&](services::EntityHandle e, glm::vec4 tint)
        {
            events::ui::AddUIImageComponentCommand a; a.entity = e; d.execute(a);
            services::UIImageData img; img.colorTint = tint;
            events::ui::SetUIImageDataCommand s; s.entity = e; s.imageData = img; d.execute(s);
        };
        auto addLabel = [&](services::EntityHandle e, const std::string& text, uint8_t hAlign, uint8_t vAlign)
        {
            events::ui::AddUILabelComponentCommand a; a.entity = e; d.execute(a);
            services::UILabelData lab; lab.text = text; lab.horizontalAlignment = hAlign; lab.verticalAlignment = vAlign;
            events::ui::SetUILabelDataCommand s; s.entity = e; s.labelData = lab; d.execute(s);
        };

        switch (type)
        {
        case WidgetType::Checkbox:
        {
            // Root already has UICheckbox; add a box image + a left "Check" square + a "Label"
            // to its right, and enable label-click toggling.
            addImage(root, glm::vec4(0.20f, 0.20f, 0.24f, 1.0f));

            services::EntityHandle check = createChild(root, "Check");
            if (check.isValid())
            {
                setRect(check, {0.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, 0.5f}, {26.0f, 26.0f}, {6.0f, 0.0f});
                addImage(check, glm::vec4(0.30f, 0.70f, 1.0f, 1.0f));
            }
            services::EntityHandle label = createChild(root, "Label");
            if (label.isValid())
            {
                setRect(label, {0.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, 0.5f}, {150.0f, 40.0f}, {40.0f, 0.0f});
                addLabel(label, "Checkbox", 0 /*Left*/, 1 /*Middle*/);
            }

            services::UICheckboxData cb;
            cb.labelToggle = true;
            events::ui::SetUICheckboxDataCommand s; s.entity = root; s.checkboxData = cb;
            d.execute(s);
            break;
        }
        case WidgetType::Dropdown:
        {
            // Root already has UIDropdown; add a header image + a "Value" label, and seed options
            // so the (preview-expanded) list is non-empty.
            addImage(root, glm::vec4(0.25f, 0.25f, 0.25f, 1.0f));

            services::EntityHandle value = createChild(root, "Value");
            if (value.isValid())
            {
                setRect(value, {0.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, 0.5f}, {180.0f, 40.0f}, {10.0f, 0.0f});
                addLabel(value, "Select...", 0 /*Left*/, 1 /*Middle*/);
            }

            services::UIDropdownData dd;
            dd.options = { {"Option 1", {}}, {"Option 2", {}}, {"Option 3", {}} };
            events::ui::SetUIDropdownDataCommand s; s.entity = root; s.dropdownData = dd;
            d.execute(s);
            break;
        }
        case WidgetType::Tabs:
        {
            // Root already has UITabs + UIRect. Build the tab bar + two tabs/panes via the shared
            // authoring helper so the auto-assembled shape matches the inspector's "+ Add Tab".
            details::UITabsDrawer::addTab(root);
            details::UITabsDrawer::addTab(root);

            services::UITabsData td;
            td.activeTabIndex = 0;
            events::ui::SetUITabsDataCommand s; s.entity = root; s.tabsData = td;
            d.execute(s);
            details::UITabsDrawer::syncPaneVisibility(root, 0);
            break;
        }
        case WidgetType::Tooltip:
        {
            // Root already has UITooltip; make it a hoverable host (image + label) in Text mode.
            addImage(root, glm::vec4(0.20f, 0.40f, 0.65f, 1.0f));

            services::EntityHandle label = createChild(root, "Label");
            if (label.isValid())
            {
                setRect(label, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});
                addLabel(label, "Hover me", 1 /*Center*/, 1 /*Middle*/);
            }

            services::UITooltipData tip;
            tip.mode = 0; // Text
            tip.text = "Tooltip text";
            events::ui::SetUITooltipDataCommand s; s.entity = root; s.tooltipData = tip;
            d.execute(s);
            break;
        }
        default:
            break; // simple widget types need no extra composition
        }
    }

    // =========================================================================
    // Draw — window shell
    // =========================================================================

    void UILayerBuilderWindow::draw()
    {
        if (!visible) return;

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("UILayerBuilder", ImVec2(1200, 700));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        std::string title = "UI Layer Builder";
        if (!layerPath.empty())
        {
            std::string filename = layerPath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
            title += " - " + filename;
        }
        else if (hasLayer())
        {
            title += " - <unsaved>";
        }
        if (dirty) title += " *";
        title += "###UILayerBuilder";

        bool wasVisible = visible;
        if (ImGui::Begin(title.c_str(), &visible, maximizer.windowFlags()))
        {
            maximizer.drawButton();
            drawToolbar();
            ImGui::Separator();

            if (!hasLayer())
            {
                ImGui::TextDisabled("Create a new layer or open a .vfPrefab to start.");
            }
            else
            {
                // Three panes: palette+hierarchy (left), canvas (center), inspector (right).
                // leftPaneWidth/rightPaneWidth are user-draggable via the vertical splitters below
                // so long hierarchy entity names stay readable.
                const float thickness = 6.0f;
                const float minLeft = 140.0f, minRight = 220.0f, minCanvas = 220.0f;
                const float totalW = ImGui::GetContentRegionAvail().x;

                leftPaneWidth = std::clamp(leftPaneWidth, minLeft,
                                           std::max(minLeft, totalW - rightPaneWidth - minCanvas - 2.0f * thickness));
                rightPaneWidth = std::clamp(rightPaneWidth, minRight,
                                            std::max(minRight, totalW - leftPaneWidth - minCanvas - 2.0f * thickness));
                const float canvasW = std::max(minCanvas, totalW - leftPaneWidth - rightPaneWidth - 2.0f * thickness);

                // Draggable vertical splitter: adjusts `size` by the horizontal drag * sign, clamped.
                auto verticalSplitter = [](const char* id, float& size, float sign,
                                           float minSize, float maxSize, float w)
                {
                    ImGui::SameLine();
                    ImGui::InvisibleButton(id, ImVec2(w, ImGui::GetContentRegionAvail().y));
                    if (ImGui::IsItemActive())
                        size = std::clamp(size + ImGui::GetIO().MouseDelta.x * sign, minSize, maxSize);
                    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
                    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    const ImU32 col = hot ? IM_COL32(130, 130, 150, 255) : IM_COL32(60, 60, 70, 255);
                    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), col);
                };

                // Zero item-spacing so the children + splitters tile exactly to totalW.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

                if (ImGui::BeginChild("LeftPane", ImVec2(leftPaneWidth, 0), ImGuiChildFlags_Borders))
                {
                    drawPalettePane();
                    ImGui::Separator();
                    hierarchyPane.draw();
                }
                ImGui::EndChild();

                verticalSplitter("##splitLeft", leftPaneWidth, +1.0f, minLeft,
                                 std::max(minLeft, totalW - rightPaneWidth - minCanvas - 2.0f * thickness), thickness);

                ImGui::SameLine();
                if (ImGui::BeginChild("CanvasPane", ImVec2(canvasW, 0), ImGuiChildFlags_Borders))
                {
                    canvasPane.draw();
                }
                ImGui::EndChild();

                verticalSplitter("##splitRight", rightPaneWidth, -1.0f, minRight,
                                 std::max(minRight, totalW - leftPaneWidth - minCanvas - 2.0f * thickness), thickness);

                ImGui::SameLine();
                if (ImGui::BeginChild("InspectorPane", ImVec2(0, 0), ImGuiChildFlags_Borders))
                {
                    drawInspectorPane();
                }
                ImGui::EndChild();

                ImGui::PopStyleVar();
            }
        }
        ImGui::End();

        // Closed via the title-bar X: persist the window size, then tear down sandbox + preview.
        if (wasVisible && !visible)
        {
            if (!sizeSaved)
            {
                editor::preview::rememberWindowSize("UILayerBuilder", maximizer.effectiveSize());
                sizeSaved = true;
            }
            closeLayer();
        }
    }

    void UILayerBuilderWindow::drawToolbar()
    {
        if (ImGui::Button("New"))
        {
            newLayer();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            std::string path = fileDialog.openFileDialog({{L"VF Prefab (*.vfPrefab)", L"*.vfPrefab"}});
            if (!path.empty()) openLayer(path);
        }
        ImGui::SameLine();
        bool noLayer = !hasLayer();
        if (noLayer) ImGui::BeginDisabled();
        if (ImGui::Button("Save"))
        {
            saveLayer(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As"))
        {
            saveLayer(true);
        }
        if (noLayer) ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Reference resolution selector.
        ImGui::SetNextItemWidth(200);
        const char* current = kResPresets[refPreset].name;
        if (ImGui::BeginCombo("Resolution", current))
        {
            for (int i = 0; i < static_cast<int>(std::size(kResPresets)); ++i)
            {
                bool selected = (refPreset == i);
                if (ImGui::Selectable(kResPresets[i].name, selected))
                {
                    refPreset = i;
                    if (i != kCustomPresetIndex)
                    {
                        refWidth = kResPresets[i].w;
                        refHeight = kResPresets[i].h;
                        applyReferenceResolution();
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (refPreset == kCustomPresetIndex)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            bool changed = ImGui::InputInt("W", &refWidth, 0, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            changed |= ImGui::InputInt("H", &refHeight, 0, 0);
            if (changed && ImGui::IsItemDeactivatedAfterEdit())
            {
                refWidth = std::clamp(refWidth, 16, 8192);
                refHeight = std::clamp(refHeight, 16, 8192);
                applyReferenceResolution();
            }
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Zoom", &zoom, 0.1f, 4.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::Button("Fit")) { zoom = 1.0f; panRef = glm::vec2(0.0f); }
        ImGui::SameLine();
        ImGui::Checkbox("Anchors", &showAnchors);
        ImGui::SameLine();
        ImGui::Checkbox("Pivots", &showPivots);

        // Theme picker.
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Theme..."))
        {
            std::string path = fileDialog.openFileDialog({{L"VF Theme (*.vfTheme)", L"*.vfTheme"}});
            if (!path.empty())
            {
                themePath = path;
                events::ui::SetCanvasThemeCommand setTheme;
                setTheme.entity = canvasRoot;
                setTheme.themePath = path;
                Dispatcher::instance().execute(setTheme);

                events::ui::ReapplyUIThemeCommand reapply;
                reapply.canvas = canvasRoot;
                Dispatcher::instance().execute(reapply);

                dirty = true;
                rebuildPreview();
            }
        }
    }

    void UILayerBuilderWindow::applyReferenceResolution()
    {
        if (!hasLayer()) return;

        // Update the canvas component's reference dims, then resize the offscreen target.
        events::ui::GetUICanvasDataQuery getQ;
        getQ.entity = canvasRoot;
        services::UICanvasData data = Dispatcher::instance().query(getQ).value_or(services::UICanvasData{});
        data.referenceWidth = static_cast<float>(refWidth);
        data.referenceHeight = static_cast<float>(refHeight);
        events::ui::SetUICanvasDataCommand setQ;
        setQ.entity = canvasRoot;
        setQ.canvasData = data;
        Dispatcher::instance().execute(setQ);

        services::events::uilayerpreview::SetUILayerReferenceResolutionCommand cmd;
        cmd.instanceId = instanceId();
        cmd.refWidth = static_cast<uint32_t>(std::max(1, refWidth));
        cmd.refHeight = static_cast<uint32_t>(std::max(1, refHeight));
        Dispatcher::instance().execute(cmd);

        dirty = true;
        rebuildPreview();
    }

    // =========================================================================
    // Palette pane
    // =========================================================================

    void UILayerBuilderWindow::drawPalettePane()
    {
        ImGui::SeparatorText("Palette");

        struct PaletteItem { const char* label; WidgetType type; };
        static const PaletteItem items[] = {
            {"Panel", WidgetType::Panel},
            {"Label", WidgetType::Label},
            {"Button", WidgetType::Button},
            {"Image", WidgetType::Image},
            {"Text Input", WidgetType::TextInput},
            {"Checkbox", WidgetType::Checkbox},
            {"Slider", WidgetType::Slider},
            {"Progress Bar", WidgetType::ProgressBar},
            {"Scroll View", WidgetType::ScrollView},
            {"Layout Group", WidgetType::LayoutGroup},
            {"List View", WidgetType::ListView},
            {"Dropdown", WidgetType::Dropdown},
            {"Tabs", WidgetType::Tabs},
            {"Tooltip", WidgetType::Tooltip},
        };

        for (const auto& item : items)
        {
            if (ImGui::Button(item.label, ImVec2(-1, 0)))
            {
                addWidget(item.type);
            }
        }
    }

    // =========================================================================
    // Inspector pane (reuse the existing UI*Drawer set for the selection)
    // =========================================================================

    void UILayerBuilderWindow::drawInspectorPane()
    {
        ImGui::SeparatorText("Inspector");

        services::EntityHandle sel = selectedEntity();
        if (!sel.isValid())
        {
            ImGui::TextDisabled("Select an element on the canvas or in the hierarchy.");
            return;
        }

        // Snapshot ALL editable UI component data BEFORE the drawers run, so a completed
        // inspector edit (any field across any UI*Drawer) coalesces into one undo entry.
        // Re-snapshot only while no edit session is in flight (and whenever the selection
        // changes), so the "before" stays the pre-edit state through a multi-frame drag.
        if (inspectorEditEntity != sel)
        {
            inspectorEditEntity = sel;
            inspectorEditActive = false;
        }
        if (!inspectorEditActive)
        {
            inspectorSnapshotBefore = uilayer::captureUISnapshot(sel);
        }

        // VK-1442 — advisory strip for compound widgets whose child composition is malformed.
        drawCompoundValidationStrip(sel);

        // The drawers each read/write component data through the same CQRS the canvas uses,
        // so edits show up live in the preview after a rebuild.
        uiCanvasDrawer.draw(sel);
        uiRectDrawer.draw(sel);
        uiImageDrawer.draw(sel);
        uiLabelDrawer.draw(sel);
        uiScrollDrawer.draw(sel);
        uiLayoutGroupDrawer.draw(sel);
        uiButtonDrawer.draw(sel);
        uiTextInputDrawer.draw(sel);
        uiCheckboxDrawer.draw(sel);
        uiDropdownDrawer.draw(sel);
        uiTabsDrawer.draw(sel);
        // The tabs drawer's "+ Add Tab" / "- Remove Tab" buttons create/delete entities; that
        // structural change lands on the mouse-release frame (which the IsAnyItemActive rebuild
        // below misses), so rebuild the preview explicitly when it reports one.
        if (uiTabsDrawer.consumeStructuralChange())
        {
            dirty = true;
            rebuildPreview();
        }
        uiSliderDrawer.draw(sel);
        uiProgressBarDrawer.draw(sel);
        uiStyleDrawer.draw(sel);
        uiAnimationDrawer.draw(sel);
        uiListViewDrawer.draw(sel);
        uiWindowDrawer.draw(sel);
        uiTooltipDrawer.draw(sel);
        uiMaskDrawer.draw(sel);
        uiDraggableDrawer.draw(sel);
        uiDropTargetDrawer.draw(sel);

        // Track an in-flight edit session and push ONE undo entry when it ends. ImGui has no
        // "any item deactivated after edit" query, so detect the session end as the transition
        // from "an item is active" to "none active" while we were mid-edit. (A no-op session —
        // e.g. opening a drawer header — yields a before==after entry that undoes to itself:
        // harmless; skipping equal snapshots is a follow-up.)
        const bool anyItemActive = ImGui::IsAnyItemActive();
        bool editJustEnded = false;
        if (anyItemActive)
        {
            inspectorEditActive = true;
        }
        else if (inspectorEditActive)
        {
            inspectorEditActive = false;
            editJustEnded = true;
            uilayer::UIComponentSnapshot after = uilayer::captureUISnapshot(sel);
            auto cmd = std::make_shared<uilayer::UIComponentEditUndoCommand>(
                sel, inspectorSnapshotBefore, std::move(after), "Edit UI element");
            events::undoredo::PushUndoableCommand push;
            push.command = cmd;
            Dispatcher::instance().execute(push);
            dirty = true;
        }

        // Rebuild while an edit is in flight (and on the frame it ends) so the WYSIWYG image
        // tracks the change.
        if (anyItemActive || editJustEnded)
        {
            rebuildPreview();
        }

        // Add Component (UI-only menu). Drawn after the edit-coalescing logic so the popup's
        // own active state doesn't fold into the inspector edit session. The popup's selectables
        // dispatch the AddUI*ComponentCommand themselves; we mark dirty + rebuild after they run.
        drawAddComponentMenu(sel);
    }

    void UILayerBuilderWindow::drawCompoundValidationStrip(services::EntityHandle sel)
    {
        if (!sel.isValid()) return;

        auto& reg = scene::EntityRegistry::getRegistry();
        entt::entity e = services::internal::fromHandle(sel);
        if (!reg.valid(e)) return;

        ui_validation::CompoundWidgetStatus status = ui_validation::validateCompoundWidget(reg, e);
        if (status.errors.empty() && status.warnings.empty())
            return;

        for (const auto& err : status.errors)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
            ImGui::TextWrapped(ICON_FA_CIRCLE_EXCLAMATION " %s", err.c_str());
            ImGui::PopStyleColor();
        }
        for (const auto& warning : status.warnings)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.30f, 1.0f));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " %s", warning.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        ImGui::Spacing();
    }

    void UILayerBuilderWindow::drawAddComponentMenu(services::EntityHandle sel)
    {
        if (!sel.isValid()) return;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Build the presence flags so the popup hides already-present UI components. Counting
        // the present flags before/after the popup body lets us detect an add deterministically
        // (the popup's selectables dispatch the AddUI*ComponentCommand synchronously on click).
        auto buildPresence = [&]() {
            details::ComponentPresence p;
            p.handle = sel;
            auto has = [&](auto query) -> bool {
                query.entity = sel;
                return Dispatcher::instance().query(query);
            };
            p.hasUICanvas      = has(events::ui::HasUICanvasComponentQuery{});
            p.hasUIRect        = has(events::ui::HasUIRectComponentQuery{});
            p.hasUIImage       = has(events::ui::HasUIImageComponentQuery{});
            p.hasUILabel       = has(events::ui::HasUILabelComponentQuery{});
            p.hasUIScroll      = has(events::ui::HasUIScrollComponentQuery{});
            p.hasUILayoutGroup = has(events::ui::HasUILayoutGroupComponentQuery{});
            p.hasUIButton      = has(events::ui::HasUIButtonComponentQuery{});
            p.hasUITextInput   = has(events::ui::HasUITextInputComponentQuery{});
            p.hasUICheckbox    = has(events::ui::HasUICheckboxComponentQuery{});
            p.hasUIDropdown    = has(events::ui::HasUIDropdownComponentQuery{});
            p.hasUITabs        = has(events::ui::HasUITabsComponentQuery{});
            p.hasUISlider      = has(events::ui::HasUISliderComponentQuery{});
            p.hasUIProgressBar = has(events::ui::HasUIProgressBarComponentQuery{});
            p.hasUIAnimation   = has(events::ui::HasUIAnimationComponentQuery{});
            p.hasUIStyle       = has(events::ui::HasUIStyleComponentQuery{});
            p.hasUIListView    = has(events::ui::HasUIListViewComponentQuery{});
            p.hasUIWindow      = has(events::ui::HasUIWindowComponentQuery{});
            p.hasUITooltip     = has(events::ui::HasUITooltipComponentQuery{});
            p.hasUIMask        = has(events::ui::HasUIMaskComponentQuery{});
            p.hasUIDraggable   = has(events::ui::HasUIDraggableComponentQuery{});
            p.hasUIDropTarget  = has(events::ui::HasUIDropTargetComponentQuery{});
            return p;
        };
        auto presentCount = [](const details::ComponentPresence& p) {
            return int(p.hasUICanvas) + int(p.hasUIRect) + int(p.hasUIImage) + int(p.hasUILabel)
                 + int(p.hasUIScroll) + int(p.hasUILayoutGroup) + int(p.hasUIButton)
                 + int(p.hasUITextInput) + int(p.hasUICheckbox) + int(p.hasUIDropdown)
                 + int(p.hasUITabs) + int(p.hasUISlider) + int(p.hasUIProgressBar)
                 + int(p.hasUIAnimation) + int(p.hasUIStyle) + int(p.hasUIListView)
                 + int(p.hasUIWindow) + int(p.hasUITooltip) + int(p.hasUIMask)
                 + int(p.hasUIDraggable) + int(p.hasUIDropTarget);
        };

        details::ComponentPresence presence = buildPresence();

        if (ImGui::Button("Add Component", ImVec2(-1, 0)))
        {
            ImGui::OpenPopup("UILayerAddComponent");
        }

        bool addedSomething = false;
        if (ImGui::BeginPopup("UILayerAddComponent"))
        {
            const int before = presentCount(presence);
            addComponentPopup.drawUISection(presence);
            // Re-query after the body: a clicked selectable already dispatched its Add command.
            if (presentCount(buildPresence()) != before)
                addedSomething = true;
            ImGui::EndPopup();
        }

        if (addedSomething)
        {
            dirty = true;
            rebuildPreview();
        }
    }
}
