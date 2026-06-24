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
#include "events/render/UILayerPreviewEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ResourceEvents.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
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

    UILayerBuilderWindow::UILayerBuilderWindow() = default;

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

        // LoadPrefab into the scene (parent = root); the loaded root becomes the sandbox.
        events::scene::LoadPrefabCommand loadCmd;
        loadCmd.filePath = path;
        auto loaded = Dispatcher::instance().execute(loadCmd);
        if (!loaded.has_value() || !loaded->isValid())
        {
            return;
        }
        canvasRoot = *loaded;

        // Tag the loaded root so it is isolated like a fresh sandbox.
        events::ui::MarkUIPreviewSandboxCommand markCmd;
        markCmd.entity = canvasRoot;
        markCmd.tagged = true;
        Dispatcher::instance().execute(markCmd);

        // Adopt the canvas's own reference resolution for the WYSIWYG extent.
        events::ui::GetUICanvasDataQuery canvasQuery;
        canvasQuery.entity = canvasRoot;
        if (auto canvasData = Dispatcher::instance().query(canvasQuery))
        {
            refWidth = static_cast<int>(canvasData->referenceWidth);
            refHeight = static_cast<int>(canvasData->referenceHeight);
            refPreset = kCustomPresetIndex;
            for (int i = 0; i < kCustomPresetIndex; ++i)
            {
                if (kResPresets[i].w == refWidth && kResPresets[i].h == refHeight) { refPreset = i; break; }
            }
        }

        layerPath = path;
        dirty = false;
        rebuildPreview();
        selectEntity(canvasRoot);
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
        dragging = false;
        dragEntity = services::EntityHandle::invalid();
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

        events::scene::SavePrefabCommand cmd;
        cmd.entity = canvasRoot;
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

        // Parent = current selection if it's part of the sandbox, else the canvas root.
        services::EntityHandle parent = selectedEntity();
        if (!parent.isValid()) parent = canvasRoot;

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
            }

            selectEntity(created);
            dirty = true;
        }

        events::undoredo::EndBatchCommand end;
        Dispatcher::instance().execute(end);

        rebuildPreview();
    }

    // =========================================================================
    // Draw — window shell
    // =========================================================================

    void UILayerBuilderWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(1100, 720), ImGuiCond_FirstUseEver);
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
        if (ImGui::Begin(title.c_str(), &visible))
        {
            drawToolbar();
            ImGui::Separator();

            if (!hasLayer())
            {
                ImGui::TextDisabled("Create a new layer or open a .vfPrefab to start.");
            }
            else
            {
                // Three panes: palette+hierarchy (left), canvas (center), inspector (right).
                const float leftW = 220.0f;
                const float rightW = 340.0f;
                if (ImGui::BeginChild("LeftPane", ImVec2(leftW, 0), ImGuiChildFlags_Borders))
                {
                    drawPalettePane();
                    ImGui::Separator();
                    drawHierarchyPane();
                }
                ImGui::EndChild();

                ImGui::SameLine();
                if (ImGui::BeginChild("CanvasPane", ImVec2(-rightW, 0), ImGuiChildFlags_Borders))
                {
                    drawCanvasPane();
                }
                ImGui::EndChild();

                ImGui::SameLine();
                if (ImGui::BeginChild("InspectorPane", ImVec2(0, 0), ImGuiChildFlags_Borders))
                {
                    drawInspectorPane();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();

        // Closed via the title-bar X: tear down the sandbox + preview.
        if (wasVisible && !visible)
        {
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
    // Hierarchy pane
    // =========================================================================

    void UILayerBuilderWindow::drawHierarchyPane()
    {
        ImGui::SeparatorText("Hierarchy");
        if (!hasLayer()) return;

        if (ImGui::BeginChild("HierTree", ImVec2(0, 0)))
        {
            drawHierarchyNode(canvasRoot, 0);
        }
        ImGui::EndChild();
    }

    void UILayerBuilderWindow::drawHierarchyNode(services::EntityHandle entity, int depth)
    {
        if (!entity.isValid()) return;

        events::scene::GetEntityQuery q;
        q.entity = entity;
        auto data = Dispatcher::instance().query(q);
        if (!data.has_value()) return;

        const bool isSelected = (selectedEntity() == entity);
        const bool hasChildren = !data->children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

        ImGui::PushID(static_cast<int>(entity.id));
        bool open = ImGui::TreeNodeEx(data->name.empty() ? "(unnamed)" : data->name.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            selectEntity(entity);
        }

        // Delete (not the canvas root).
        if (entity != canvasRoot && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete"))
            {
                events::scene::DeleteEntityCommand del;
                del.entity = entity;
                Dispatcher::instance().execute(del);
                if (isSelected) selectEntity(canvasRoot);
                dirty = true;
                rebuildPreview();
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            for (auto child : data->children)
            {
                drawHierarchyNode(child, depth + 1);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    // =========================================================================
    // Canvas pane (offscreen image + handles)
    // =========================================================================

    void UILayerBuilderWindow::drawCanvasPane()
    {
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        glm::vec2 regionOrigin(cursor.x, cursor.y);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        glm::vec2 regionSize(avail.x, avail.y);
        if (regionSize.x <= 0.0f || regionSize.y <= 0.0f) return;

        drawCanvasImageAndHandles(regionOrigin, regionSize);
    }

    void UILayerBuilderWindow::drawCanvasImageAndHandles(glm::vec2 regionOrigin, glm::vec2 regionSize)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Backdrop (checker-ish dark fill) so the letterboxed area is visible.
        dl->AddRectFilled(ImVec2(regionOrigin.x, regionOrigin.y),
                          ImVec2(regionOrigin.x + regionSize.x, regionOrigin.y + regionSize.y),
                          IM_COL32(28, 28, 32, 255));

        glm::vec2 refExtent = canvasReferenceExtent();
        uilayer::LetterboxMapping map =
            uilayer::makeLetterbox(regionOrigin, regionSize, refExtent, zoom, panRef);

        // The offscreen image, drawn letterboxed.
        void* tex = renderPreview();
        ImVec2 imgTL(map.originScreen.x, map.originScreen.y);
        ImVec2 imgBR(map.originScreen.x + map.imageSizeScreen.x,
                     map.originScreen.y + map.imageSizeScreen.y);
        if (tex)
        {
            dl->AddImage(tex, imgTL, imgBR);
        }
        else
        {
            dl->AddRectFilled(imgTL, imgBR, IM_COL32(15, 15, 18, 255));
        }
        // Canvas border.
        dl->AddRect(imgTL, imgBR, IM_COL32(90, 90, 100, 255));

        // An invisible button over the content region captures mouse input for the canvas.
        ImGui::SetCursorScreenPos(ImVec2(regionOrigin.x, regionOrigin.y));
        ImGui::InvisibleButton("##canvas", ImVec2(regionSize.x, regionSize.y),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        handleCanvasInput(map);

        // Selection handles overlay (drawn after input so it reflects the live rect).
        services::EntityHandle sel = selectedEntity();
        if (sel.isValid())
        {
            if (auto rect = resolvedRectOf(sel))
            {
                drawHandleOverlay(dl, map, *rect);
            }
        }
    }

    void UILayerBuilderWindow::drawHandleOverlay(ImDrawList* dl, const uilayer::LetterboxMapping& map,
                                                 const uilayer::RefRect& rect)
    {
        glm::vec2 tl = map.refToScreen(glm::vec2(rect.x, rect.y));
        glm::vec2 br = map.refToScreen(glm::vec2(rect.right(), rect.bottom()));

        // Selection outline.
        dl->AddRect(ImVec2(tl.x, tl.y), ImVec2(br.x, br.y), IM_COL32(255, 180, 40, 255), 0.0f, 0, 1.5f);

        // 8 resize handles.
        const float hs = 4.0f; // half-size in screen px
        auto positions = uilayer::handlePositions(rect);
        for (const auto& p : positions)
        {
            glm::vec2 s = map.refToScreen(p);
            dl->AddRectFilled(ImVec2(s.x - hs, s.y - hs), ImVec2(s.x + hs, s.y + hs),
                              IM_COL32(255, 180, 40, 255));
            dl->AddRect(ImVec2(s.x - hs, s.y - hs), ImVec2(s.x + hs, s.y + hs), IM_COL32(20, 20, 20, 255));
        }

        // Anchor markers (the element's anchor rectangle on the canvas).
        if (showAnchors)
        {
            if (auto data = rectDataOf(selectedEntity()))
            {
                glm::vec2 ext = canvasReferenceExtent();
                float aL = data->anchorMin.x * ext.x;
                float aR = data->anchorMax.x * ext.x;
                float aT = (1.0f - data->anchorMax.y) * ext.y;
                float aB = (1.0f - data->anchorMin.y) * ext.y;
                glm::vec2 atl = map.refToScreen(glm::vec2(aL, aT));
                glm::vec2 abr = map.refToScreen(glm::vec2(aR, aB));
                dl->AddRect(ImVec2(atl.x, atl.y), ImVec2(abr.x, abr.y), IM_COL32(80, 160, 255, 200), 0.0f,
                            0, 1.0f);
            }
        }

        // Pivot marker.
        if (showPivots)
        {
            if (auto data = rectDataOf(selectedEntity()))
            {
                float px = rect.x + data->pivot.x * rect.w;
                float py = rect.y + (1.0f - data->pivot.y) * rect.h; // pivot.y is bottom-up
                glm::vec2 s = map.refToScreen(glm::vec2(px, py));
                dl->AddCircle(ImVec2(s.x, s.y), 5.0f, IM_COL32(40, 255, 120, 255), 12, 1.5f);
                dl->AddLine(ImVec2(s.x - 7, s.y), ImVec2(s.x + 7, s.y), IM_COL32(40, 255, 120, 255));
                dl->AddLine(ImVec2(s.x, s.y - 7), ImVec2(s.x, s.y + 7), IM_COL32(40, 255, 120, 255));
            }
        }
    }

    void UILayerBuilderWindow::handleCanvasInput(const uilayer::LetterboxMapping& map)
    {
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImGuiIO& io = ImGui::GetIO();
        glm::vec2 mouseScreen(io.MousePos.x, io.MousePos.y);
        glm::vec2 mouseRef = map.screenToRef(mouseScreen);

        // Middle-drag pans the canvas.
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            glm::vec2 deltaScreen(io.MouseDelta.x, io.MouseDelta.y);
            if (map.scale > 0.0f) panRef += deltaScreen / map.scale;
        }

        // Left mouse: select / start a handle drag.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            services::EntityHandle sel = selectedEntity();

            // First, if a handle of the current selection is under the cursor, begin a drag.
            uilayer::HandleKind handle = uilayer::HandleKind::None;
            if (sel.isValid())
            {
                if (auto rect = resolvedRectOf(sel))
                {
                    float grabHalfRef = (map.scale > 0.0f) ? (6.0f / map.scale) : 6.0f;
                    handle = uilayer::hitTestHandle(*rect, mouseRef, grabHalfRef);
                    if (handle != uilayer::HandleKind::None)
                    {
                        if (auto before = rectDataOf(sel))
                        {
                            dragging = true;
                            activeHandle = handle;
                            dragEntity = sel;
                            dragBefore = *before;
                            dragStartRect = *rect;
                            dragStartRefMouse = mouseRef;
                        }
                    }
                }
            }

            // No handle hit: pick the top-most element under the cursor (CPU hit-test
            // through the preview's reference-extent picker) and select it.
            if (handle == uilayer::HandleKind::None)
            {
                services::events::uilayerpreview::PickUILayerElementAtQuery pick;
                pick.instanceId = instanceId();
                pick.refPx = mouseRef;
                auto hit = Dispatcher::instance().query(pick);
                selectEntity(hit.isValid() ? hit : canvasRoot);
            }
        }

        // Dragging a handle: dispatch live SetUIRectDataCommand (no undo push mid-drag).
        if (dragging && dragEntity.isValid())
        {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                glm::vec2 deltaRef = mouseRef - dragStartRefMouse;
                uilayer::RefRect target = uilayer::applyHandleDrag(dragStartRect, activeHandle, deltaRef);

                glm::vec2 ext = canvasReferenceExtent();
                services::UIRectData solved =
                    uilayer::solveRectData(dragBefore, target, ext.x, ext.y);

                events::ui::SetUIRectDataCommand cmd;
                cmd.entity = dragEntity;
                cmd.rectData = solved;
                Dispatcher::instance().execute(cmd);
                rebuildPreview();
            }

            // Mouse released: coalesce the whole drag into one undo entry.
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (auto after = rectDataOf(dragEntity))
                {
                    const char* verb = (activeHandle == uilayer::HandleKind::Body) ? "Move" : "Resize";
                    pushRectEditUndo(dragEntity, dragBefore, *after, std::string(verb) + " UI element");
                }
                dragging = false;
                activeHandle = uilayer::HandleKind::None;
                dragEntity = services::EntityHandle::invalid();
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
        uiSliderDrawer.draw(sel);
        uiProgressBarDrawer.draw(sel);
        uiStyleDrawer.draw(sel);

        // Track an in-flight edit session; on edit-completion push ONE undo entry holding the
        // before/after snapshots. IsAnyItemDeactivatedAfterEdit fires only on a real value
        // change, so this never records a no-op entry (covers label text, colors, every field).
        if (ImGui::IsAnyItemActive())
        {
            inspectorEditActive = true;
        }
        if (ImGui::IsAnyItemDeactivatedAfterEdit())
        {
            inspectorEditActive = false;
            uilayer::UIComponentSnapshot after = uilayer::captureUISnapshot(sel);
            auto cmd = std::make_shared<uilayer::UIComponentEditUndoCommand>(
                sel, inspectorSnapshotBefore, std::move(after), "Edit UI element");
            events::undoredo::PushUndoableCommand push;
            push.command = cmd;
            Dispatcher::instance().execute(push);
            dirty = true;
        }

        // Rebuild every frame an edit is in flight so the WYSIWYG image tracks the change.
        if (ImGui::IsAnyItemActive() || inspectorEditActive)
        {
            rebuildPreview();
        }
    }
}
