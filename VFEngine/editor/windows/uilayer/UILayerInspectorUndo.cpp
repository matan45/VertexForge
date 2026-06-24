#include "UILayerInspectorUndo.hpp"

#include "events/EventDispatcher.hpp"
#include "events/ui/UICanvasRectImageEvents.hpp"
#include "events/ui/UILabelButtonEvents.hpp"
#include "events/ui/UIInputCheckboxEvents.hpp"
#include "events/ui/UIScrollLayoutEvents.hpp"
#include "events/ui/UISliderProgressEvents.hpp"
#include "events/ui/UIDropdownTabsEvents.hpp"
#include "events/ui/UIThemeEvents.hpp"

namespace windows::uilayer
{
    namespace
    {
        using Dispatcher = events::EventDispatcher;

        // query<Q>() helper: each Get*DataQuery returns std::optional<...Data>.
        template <typename Query>
        auto runQuery(services::EntityHandle entity)
        {
            Query q;
            q.entity = entity;
            return Dispatcher::instance().query(q);
        }
    }

    UIComponentSnapshot captureUISnapshot(services::EntityHandle entity)
    {
        UIComponentSnapshot s;
        if (!entity.isValid()) return s;

        s.canvas = runQuery<events::ui::GetUICanvasDataQuery>(entity);
        s.rect = runQuery<events::ui::GetUIRectDataQuery>(entity);
        s.image = runQuery<events::ui::GetUIImageDataQuery>(entity);
        s.label = runQuery<events::ui::GetUILabelDataQuery>(entity);
        s.scroll = runQuery<events::ui::GetUIScrollDataQuery>(entity);
        s.layoutGroup = runQuery<events::ui::GetUILayoutGroupDataQuery>(entity);
        s.button = runQuery<events::ui::GetUIButtonDataQuery>(entity);
        s.textInput = runQuery<events::ui::GetUITextInputDataQuery>(entity);
        s.checkbox = runQuery<events::ui::GetUICheckboxDataQuery>(entity);
        s.dropdown = runQuery<events::ui::GetUIDropdownDataQuery>(entity);
        s.tabs = runQuery<events::ui::GetUITabsDataQuery>(entity);
        s.slider = runQuery<events::ui::GetUISliderDataQuery>(entity);
        s.progressBar = runQuery<events::ui::GetUIProgressBarDataQuery>(entity);
        s.styleKey = runQuery<events::ui::GetUIStyleKeyQuery>(entity);
        return s;
    }

    void applyUISnapshot(services::EntityHandle entity, const UIComponentSnapshot& s)
    {
        if (!entity.isValid()) return;

        auto& dispatcher = Dispatcher::instance();

        if (s.canvas)
        {
            events::ui::SetUICanvasDataCommand cmd; cmd.entity = entity; cmd.canvasData = *s.canvas;
            dispatcher.execute(cmd);
        }
        if (s.rect)
        {
            events::ui::SetUIRectDataCommand cmd; cmd.entity = entity; cmd.rectData = *s.rect;
            dispatcher.execute(cmd);
        }
        if (s.image)
        {
            events::ui::SetUIImageDataCommand cmd; cmd.entity = entity; cmd.imageData = *s.image;
            dispatcher.execute(cmd);
        }
        if (s.label)
        {
            events::ui::SetUILabelDataCommand cmd; cmd.entity = entity; cmd.labelData = *s.label;
            dispatcher.execute(cmd);
        }
        if (s.scroll)
        {
            events::ui::SetUIScrollDataCommand cmd; cmd.entity = entity; cmd.scrollData = *s.scroll;
            dispatcher.execute(cmd);
        }
        if (s.layoutGroup)
        {
            events::ui::SetUILayoutGroupDataCommand cmd; cmd.entity = entity;
            cmd.layoutGroupData = *s.layoutGroup;
            dispatcher.execute(cmd);
        }
        if (s.button)
        {
            events::ui::SetUIButtonDataCommand cmd; cmd.entity = entity; cmd.buttonData = *s.button;
            dispatcher.execute(cmd);
        }
        if (s.textInput)
        {
            events::ui::SetUITextInputDataCommand cmd; cmd.entity = entity;
            cmd.textInputData = *s.textInput;
            dispatcher.execute(cmd);
        }
        if (s.checkbox)
        {
            events::ui::SetUICheckboxDataCommand cmd; cmd.entity = entity; cmd.checkboxData = *s.checkbox;
            dispatcher.execute(cmd);
        }
        if (s.dropdown)
        {
            events::ui::SetUIDropdownDataCommand cmd; cmd.entity = entity; cmd.dropdownData = *s.dropdown;
            dispatcher.execute(cmd);
        }
        if (s.tabs)
        {
            events::ui::SetUITabsDataCommand cmd; cmd.entity = entity; cmd.tabsData = *s.tabs;
            dispatcher.execute(cmd);
        }
        if (s.slider)
        {
            events::ui::SetUISliderDataCommand cmd; cmd.entity = entity; cmd.sliderData = *s.slider;
            dispatcher.execute(cmd);
        }
        if (s.progressBar)
        {
            events::ui::SetUIProgressBarDataCommand cmd; cmd.entity = entity;
            cmd.progressBarData = *s.progressBar;
            dispatcher.execute(cmd);
        }
        if (s.styleKey)
        {
            events::ui::SetUIStyleKeyCommand cmd; cmd.entity = entity; cmd.styleKey = *s.styleKey;
            dispatcher.execute(cmd);
        }
    }

    UIComponentEditUndoCommand::UIComponentEditUndoCommand(services::EntityHandle entity,
                                                           UIComponentSnapshot before,
                                                           UIComponentSnapshot after,
                                                           std::string description)
        : entity(entity), before(std::move(before)), after(std::move(after)),
          description(std::move(description))
    {
    }

    void UIComponentEditUndoCommand::execute()
    {
        applyUISnapshot(entity, after);
    }

    void UIComponentEditUndoCommand::undo()
    {
        applyUISnapshot(entity, before);
    }
}
