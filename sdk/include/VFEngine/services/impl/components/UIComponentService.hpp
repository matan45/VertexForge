#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include "../../events/EventTypes.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>

namespace scene {
    class SceneGraphSystem;
}

namespace events {
    class EventDispatcher;
}

namespace services {

    class UIComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit UIComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // UI Canvas Operations
        bool addUICanvasComponent(EntityHandle entity);
        bool removeUICanvasComponent(EntityHandle entity);
        bool hasUICanvasComponent(EntityHandle entity) const;
        std::optional<UICanvasData> getUICanvasData(EntityHandle entity) const;
        bool setUICanvasData(EntityHandle entity, const UICanvasData& canvasData);

        // UI Rect Operations
        bool addUIRectComponent(EntityHandle entity);
        bool removeUIRectComponent(EntityHandle entity);
        bool hasUIRectComponent(EntityHandle entity) const;
        std::optional<UIRectData> getUIRectData(EntityHandle entity) const;
        bool setUIRectData(EntityHandle entity, const UIRectData& rectData);
        bool setUIRectPixels(EntityHandle entity, float x, float y, float w, float h);
        std::optional<UIResolvedRectData> getUIResolvedRectPixels(EntityHandle entity) const;

        // UI Image Operations
        bool addUIImageComponent(EntityHandle entity);
        bool removeUIImageComponent(EntityHandle entity);
        bool hasUIImageComponent(EntityHandle entity) const;
        std::optional<UIImageData> getUIImageData(EntityHandle entity) const;
        bool setUIImageData(EntityHandle entity, const UIImageData& imageData);

        // UI Scroll Operations
        bool addUIScrollComponent(EntityHandle entity);
        bool removeUIScrollComponent(EntityHandle entity);
        bool hasUIScrollComponent(EntityHandle entity) const;
        std::optional<UIScrollData> getUIScrollData(EntityHandle entity) const;
        bool setUIScrollData(EntityHandle entity, const UIScrollData& scrollData);
        bool setScrollOffset(EntityHandle entity, const glm::vec2& offset);
        std::optional<glm::vec2> getScrollOffset(EntityHandle entity) const;

        // UI Layout Group Operations
        bool addUILayoutGroupComponent(EntityHandle entity);
        bool removeUILayoutGroupComponent(EntityHandle entity);
        bool hasUILayoutGroupComponent(EntityHandle entity) const;
        std::optional<UILayoutGroupData> getUILayoutGroupData(EntityHandle entity) const;
        bool setUILayoutGroupData(EntityHandle entity, const UILayoutGroupData& layoutGroupData);

        // UI Label Operations
        bool addUILabelComponent(EntityHandle entity);
        bool removeUILabelComponent(EntityHandle entity);
        bool hasUILabelComponent(EntityHandle entity) const;
        std::optional<UILabelData> getUILabelData(EntityHandle entity) const;
        bool setUILabelData(EntityHandle entity, const UILabelData& labelData);
        std::optional<glm::vec2> getUILabelPreferredSize(EntityHandle entity) const;

        // UI Button Operations
        bool addUIButtonComponent(EntityHandle entity);
        bool removeUIButtonComponent(EntityHandle entity);
        bool hasUIButtonComponent(EntityHandle entity) const;
        std::optional<UIButtonData> getUIButtonData(EntityHandle entity) const;
        bool setUIButtonData(EntityHandle entity, const UIButtonData& buttonData);

        // UI TextInput Operations
        bool addUITextInputComponent(EntityHandle entity);
        bool removeUITextInputComponent(EntityHandle entity);
        bool hasUITextInputComponent(EntityHandle entity) const;
        std::optional<UITextInputData> getUITextInputData(EntityHandle entity) const;
        bool setUITextInputData(EntityHandle entity, const UITextInputData& textInputData);

        // UI Checkbox Operations
        bool addUICheckboxComponent(EntityHandle entity);
        bool removeUICheckboxComponent(EntityHandle entity);
        bool hasUICheckboxComponent(EntityHandle entity) const;
        std::optional<UICheckboxData> getUICheckboxData(EntityHandle entity) const;
        bool setUICheckboxData(EntityHandle entity, const UICheckboxData& checkboxData);

        // UI Dropdown Operations
        bool addUIDropdownComponent(EntityHandle entity);
        bool removeUIDropdownComponent(EntityHandle entity);
        bool hasUIDropdownComponent(EntityHandle entity) const;
        std::optional<UIDropdownData> getUIDropdownData(EntityHandle entity) const;
        bool setUIDropdownData(EntityHandle entity, const UIDropdownData& dropdownData);
        bool setUIDropdownSelectedIndex(EntityHandle entity, int selectedIndex);
        bool openUIDropdown(EntityHandle entity);
        bool closeUIDropdown(EntityHandle entity);

        // UI Tabs Operations
        bool addUITabsComponent(EntityHandle entity);
        bool removeUITabsComponent(EntityHandle entity);
        bool hasUITabsComponent(EntityHandle entity) const;
        std::optional<UITabsData> getUITabsData(EntityHandle entity) const;
        bool setUITabsData(EntityHandle entity, const UITabsData& tabsData);
        bool selectTab(EntityHandle entity, int tabIndex);

        // UI Slider Operations
        bool addUISliderComponent(EntityHandle entity);
        bool removeUISliderComponent(EntityHandle entity);
        bool hasUISliderComponent(EntityHandle entity) const;
        std::optional<UISliderData> getUISliderData(EntityHandle entity) const;
        bool setUISliderData(EntityHandle entity, const UISliderData& sliderData);
        bool setUISliderValue(EntityHandle entity, float value);

        // UI ProgressBar Operations
        bool addUIProgressBarComponent(EntityHandle entity);
        bool removeUIProgressBarComponent(EntityHandle entity);
        bool hasUIProgressBarComponent(EntityHandle entity) const;
        std::optional<UIProgressBarData> getUIProgressBarData(EntityHandle entity) const;
        bool setUIProgressBarData(EntityHandle entity, const UIProgressBarData& progressBarData);
        bool setUIProgressBarValue(EntityHandle entity, float value);

        // UI Animation Operations
        bool addUIAnimationComponent(EntityHandle entity);
        bool removeUIAnimationComponent(EntityHandle entity);
        bool hasUIAnimationComponent(EntityHandle entity) const;
        std::optional<UIAnimationData> getUIAnimationData(EntityHandle entity) const;
        bool setUIAnimationData(EntityHandle entity, const UIAnimationData& data);
        bool playUIAnimation(EntityHandle entity);
        bool stopUIAnimation(EntityHandle entity);
        bool pauseUIAnimation(EntityHandle entity);
        bool resumeUIAnimation(EntityHandle entity);
        bool isUIAnimationPlaying(EntityHandle entity) const;

        // UI Mask Operations
        bool addUIMaskComponent(EntityHandle entity);
        bool removeUIMaskComponent(EntityHandle entity);
        bool hasUIMaskComponent(EntityHandle entity) const;
        std::optional<UIMaskData> getUIMaskData(EntityHandle entity) const;
        bool setUIMaskData(EntityHandle entity, const UIMaskData& data);

        // UI Draggable Operations
        bool addUIDraggableComponent(EntityHandle entity);
        bool removeUIDraggableComponent(EntityHandle entity);
        bool hasUIDraggableComponent(EntityHandle entity) const;
        std::optional<UIDraggableData> getUIDraggableData(EntityHandle entity) const;
        bool setUIDraggableData(EntityHandle entity, const UIDraggableData& data);

        // UI DropTarget Operations
        bool addUIDropTargetComponent(EntityHandle entity);
        bool removeUIDropTargetComponent(EntityHandle entity);
        bool hasUIDropTargetComponent(EntityHandle entity) const;
        std::optional<UIDropTargetData> getUIDropTargetData(EntityHandle entity) const;
        bool setUIDropTargetData(EntityHandle entity, const UIDropTargetData& data);

        // UI Tooltip Operations
        bool addUITooltipComponent(EntityHandle entity);
        bool removeUITooltipComponent(EntityHandle entity);
        bool hasUITooltipComponent(EntityHandle entity) const;
        std::optional<UITooltipData> getUITooltipData(EntityHandle entity) const;
        bool setUITooltipData(EntityHandle entity, const UITooltipData& data);

        // UI Window Operations
        bool addUIWindowComponent(EntityHandle entity);
        bool removeUIWindowComponent(EntityHandle entity);
        bool hasUIWindowComponent(EntityHandle entity) const;
        std::optional<UIWindowData> getUIWindowData(EntityHandle entity) const;
        bool setUIWindowData(EntityHandle entity, const UIWindowData& data);
        bool openUIWindow(EntityHandle entity);
        bool closeUIWindow(EntityHandle entity);
        bool isUIWindowOpen(EntityHandle entity) const;

        // UI ListView Operations
        bool addUIListViewComponent(EntityHandle entity);
        bool removeUIListViewComponent(EntityHandle entity);
        bool hasUIListViewComponent(EntityHandle entity) const;
        std::optional<UIListViewData> getUIListViewData(EntityHandle entity) const;
        bool setUIListViewData(EntityHandle entity, const UIListViewData& data);
        bool setUIListItemCount(EntityHandle entity, int itemCount);
        bool setUIListItemTemplate(EntityHandle entity, const std::string& templatePath);
        EntityHandle getUIListItem(EntityHandle entity, int index) const;
        bool setUIListSelectedIndex(EntityHandle entity, int selectedIndex);
        bool reconcileUIListView(entt::entity listEntity);
        void reconcileAllUIListViews();

        // UI Style / Theme Operations
        bool addUIStyleComponent(EntityHandle entity);
        bool removeUIStyleComponent(EntityHandle entity);
        bool hasUIStyleComponent(EntityHandle entity) const;
        std::optional<std::string> getUIStyleKey(EntityHandle entity) const;
        bool setUIStyleKey(EntityHandle entity, const std::string& styleKey);
        bool setCanvasTheme(EntityHandle entity, const std::string& themePath);
        std::optional<std::string> getCanvasThemePath(EntityHandle entity) const;
        int reapplyUITheme(std::optional<EntityHandle> canvas);

    private:
        int applyCanvasTheme(entt::entity canvasEntity);
        void destroyUIListInstances(entt::entity listEntity);
        events::SubscriptionToken sceneLoadedToken{};
        events::SubscriptionToken listViewSceneLoadedToken{};

        void registerCanvasRectImageHandlers(events::EventDispatcher& dispatcher);
        void registerScrollLayoutHandlers(events::EventDispatcher& dispatcher);
        void registerInteractiveHandlers(events::EventDispatcher& dispatcher);
        void registerDropdownTabsHandlers(events::EventDispatcher& dispatcher);
        void registerSliderProgressHandlers(events::EventDispatcher& dispatcher);
        void registerAnimationHandlers(events::EventDispatcher& dispatcher);
        void registerMaskHandlers(events::EventDispatcher& dispatcher);
        void registerDragDropHandlers(events::EventDispatcher& dispatcher);
        void registerThemeHandlers(events::EventDispatcher& dispatcher);
        void registerTooltipHandlers(events::EventDispatcher& dispatcher);
        void registerWindowHandlers(events::EventDispatcher& dispatcher);
        void registerListViewHandlers(events::EventDispatcher& dispatcher);
    };

}
