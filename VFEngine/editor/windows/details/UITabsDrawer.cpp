#include "UITabsDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>
#include <string>

namespace windows::details
{
    namespace
    {
        using Dispatcher = events::EventDispatcher;

        // Adds a UIRect to `e` and sets it to the given anchors/size (one helper for the many
        // little children the tab/pane composition creates).
        void seedRect(services::EntityHandle e, glm::vec2 anchorMin, glm::vec2 anchorMax,
                      glm::vec2 pivot, glm::vec2 sizeDelta, glm::vec2 anchoredPosition)
        {
            events::ui::AddUIRectComponentCommand add;
            add.entity = e;
            Dispatcher::instance().execute(add);

            services::UIRectData rect;
            rect.anchorMin = anchorMin;
            rect.anchorMax = anchorMax;
            rect.pivot = pivot;
            rect.sizeDelta = sizeDelta;
            rect.anchoredPosition = anchoredPosition;
            events::ui::SetUIRectDataCommand set;
            set.entity = e;
            set.rectData = rect;
            Dispatcher::instance().execute(set);
        }

        // Creates a centered UILabel child filling the parent.
        void addCenteredLabel(services::EntityHandle parent, const std::string& text)
        {
            events::scene::CreateEntityCommand create;
            create.name = "Label";
            create.parent = parent;
            services::EntityHandle lbl = Dispatcher::instance().execute(create);
            if (!lbl.isValid())
                return;

            seedRect(lbl, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});

            events::ui::AddUILabelComponentCommand add;
            add.entity = lbl;
            Dispatcher::instance().execute(add);

            services::UILabelData label;
            label.text = text;
            label.horizontalAlignment = 1; // Center
            label.verticalAlignment = 1;   // Middle
            events::ui::SetUILabelDataCommand set;
            set.entity = lbl;
            set.labelData = label;
            Dispatcher::instance().execute(set);
        }

        // Reads the tabs root's active index (0 when absent).
        int activeIndexOf(services::EntityHandle tabsRoot)
        {
            events::ui::GetUITabsDataQuery query;
            query.entity = tabsRoot;
            auto opt = Dispatcher::instance().query(query);
            return opt.has_value() ? opt->activeTabIndex : 0;
        }

        // Writes a clamped active index back onto the tabs root.
        void writeActiveIndex(services::EntityHandle tabsRoot, int activeIndex)
        {
            events::ui::GetUITabsDataQuery query;
            query.entity = tabsRoot;
            auto opt = Dispatcher::instance().query(query);
            services::UITabsData data = opt.value_or(services::UITabsData{});
            data.activeTabIndex = activeIndex;
            events::ui::SetUITabsDataCommand set;
            set.entity = tabsRoot;
            set.tabsData = data;
            Dispatcher::instance().execute(set);
        }
    }

    UITabsDrawer::TabsStructure UITabsDrawer::inspectStructure(services::EntityHandle tabsRoot)
    {
        TabsStructure result;
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity root = services::internal::fromHandle(tabsRoot);
        if (!registry.valid(root))
            return result;

        const auto* children = registry.try_get<components::ChildrenComponent>(root);
        if (!children)
            return result;

        entt::entity barEntt = entt::null;
        if (!children->children.empty())
        {
            entt::entity first = children->children.front();
            if (registry.valid(first) && registry.all_of<components::UILayoutGroupComponent>(first))
                barEntt = first;
        }

        if (barEntt != entt::null)
        {
            result.tabBar = services::internal::toHandle(barEntt);
            if (const auto* barKids = registry.try_get<components::ChildrenComponent>(barEntt))
                for (entt::entity bc : barKids->children)
                    if (registry.valid(bc))
                        result.tabButtons.push_back(services::internal::toHandle(bc));
        }

        for (entt::entity c : children->children)
        {
            if (!registry.valid(c) || c == barEntt)
                continue;
            result.panes.push_back(services::internal::toHandle(c));
        }
        return result;
    }

    services::EntityHandle UITabsDrawer::addTab(services::EntityHandle tabsRoot)
    {
        if (!tabsRoot.isValid())
            return services::EntityHandle::invalid();

        TabsStructure structure = inspectStructure(tabsRoot);

        // Ensure the tab bar exists (first child carrying a UILayoutGroup, laid out horizontally).
        services::EntityHandle tabBar = structure.tabBar;
        if (!tabBar.isValid())
        {
            events::scene::CreateEntityCommand create;
            create.name = "TabBar";
            create.parent = tabsRoot;
            tabBar = Dispatcher::instance().execute(create);
            if (!tabBar.isValid())
                return services::EntityHandle::invalid();

            // Stretch across the top edge, fixed height.
            seedRect(tabBar, {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 36.0f}, {0.0f, 0.0f});

            events::ui::AddUILayoutGroupComponentCommand addLayout;
            addLayout.entity = tabBar;
            Dispatcher::instance().execute(addLayout);

            services::UILayoutGroupData layout;
            layout.direction = 1; // Horizontal
            layout.spacing = 4.0f;
            events::ui::SetUILayoutGroupDataCommand setLayout;
            setLayout.entity = tabBar;
            setLayout.layoutGroupData = layout;
            Dispatcher::instance().execute(setLayout);
        }

        const int newIndex = static_cast<int>(structure.tabButtons.size());
        const std::string label = "Tab " + std::to_string(newIndex + 1);

        // Tab button: an image with a centered label, sized for the horizontal layout group.
        {
            events::scene::CreateEntityCommand create;
            create.name = label;
            create.parent = tabBar;
            services::EntityHandle button = Dispatcher::instance().execute(create);
            if (button.isValid())
            {
                seedRect(button, {0.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, 0.5f}, {90.0f, 30.0f}, {0.0f, 0.0f});
                events::ui::AddUIImageComponentCommand addImage;
                addImage.entity = button;
                Dispatcher::instance().execute(addImage);
                addCenteredLabel(button, label);
            }
        }

        // Content pane: a panel filling the area below the tab bar, with a descriptive label.
        events::scene::CreateEntityCommand createPane;
        createPane.name = "Pane " + std::to_string(newIndex + 1);
        createPane.parent = tabsRoot;
        services::EntityHandle pane = Dispatcher::instance().execute(createPane);
        if (pane.isValid())
        {
            // Full width, height minus the tab bar, shifted down to sit beneath it.
            seedRect(pane, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {0.0f, -36.0f}, {0.0f, -18.0f});

            events::ui::AddUIImageComponentCommand addImage;
            addImage.entity = pane;
            Dispatcher::instance().execute(addImage);
            services::UIImageData image;
            image.colorTint = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
            events::ui::SetUIImageDataCommand setImage;
            setImage.entity = pane;
            setImage.imageData = image;
            Dispatcher::instance().execute(setImage);

            addCenteredLabel(pane, "Pane " + std::to_string(newIndex + 1) + " content");
        }

        // Keep activeTabIndex valid and show exactly the active pane.
        TabsStructure after = inspectStructure(tabsRoot);
        const int paneCount = static_cast<int>(after.panes.size());
        int activeIndex = activeIndexOf(tabsRoot);
        if (activeIndex < 0)
            activeIndex = 0;
        if (paneCount > 0 && activeIndex >= paneCount)
            activeIndex = paneCount - 1;
        writeActiveIndex(tabsRoot, activeIndex);
        syncPaneVisibility(tabsRoot, activeIndex);

        return pane;
    }

    void UITabsDrawer::removeLastTab(services::EntityHandle tabsRoot)
    {
        if (!tabsRoot.isValid())
            return;

        TabsStructure structure = inspectStructure(tabsRoot);
        if (structure.panes.empty() && structure.tabButtons.empty())
            return;

        if (!structure.tabButtons.empty())
        {
            events::scene::DeleteEntityCommand del;
            del.entity = structure.tabButtons.back();
            Dispatcher::instance().execute(del);
        }
        if (!structure.panes.empty())
        {
            events::scene::DeleteEntityCommand del;
            del.entity = structure.panes.back();
            Dispatcher::instance().execute(del);
        }

        TabsStructure after = inspectStructure(tabsRoot);
        const int paneCount = static_cast<int>(after.panes.size());
        int activeIndex = activeIndexOf(tabsRoot);
        if (activeIndex >= paneCount)
            activeIndex = paneCount - 1;
        if (activeIndex < 0)
            activeIndex = 0;
        writeActiveIndex(tabsRoot, activeIndex);
        syncPaneVisibility(tabsRoot, activeIndex);
    }

    void UITabsDrawer::syncPaneVisibility(services::EntityHandle tabsRoot, int activeIndex)
    {
        TabsStructure structure = inspectStructure(tabsRoot);
        for (int i = 0; i < static_cast<int>(structure.panes.size()); ++i)
        {
            events::scene::SetEntityActiveCommand cmd;
            cmd.entity = structure.panes[i];
            cmd.isActive = (i == activeIndex);
            Dispatcher::instance().execute(cmd);
        }
    }

    bool UITabsDrawer::consumeStructuralChange()
    {
        bool pending = structuralChangePending_;
        structuralChangePending_ = false;
        return pending;
    }

    bool UITabsDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUITabsComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasTabs = dispatcher.query(hasQuery);

        if (!hasTabs)
        {
            return false;
        }

        events::ui::GetUITabsDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UITabsComponent");

        bool removeTabs = false;
        bool isOpen = drawHeader(removeTabs);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UITabsData data = *dataOpt;
            const int paneCount = static_cast<int>(inspectStructure(handle).panes.size());
            bool dataChanged = false;
            bool activeChanged = false;

            ImGui::TextDisabled("Tabbed panel container with switchable content views");
            ImGui::Spacing();

            dataChanged |= drawTabBarPosition(data);
            activeChanged = drawActiveTabSelector(data, paneCount);
            ImGui::Spacing();

            // + Add / - Remove tab buttons (structural; uses the shared static helpers).
            const bool structural = drawTabAuthoring(handle, paneCount);

            ImGui::Spacing();
            drawCurrentState(data);

            if ((dataChanged || activeChanged) && !structural)
            {
                events::ui::SetUITabsDataCommand cmd;
                cmd.entity = handle;
                cmd.tabsData = data;
                dispatcher.execute(cmd);
            }
            if (activeChanged && !structural)
            {
                // Reflect the selected tab in the (preview-visible) panes.
                syncPaneVisibility(handle, data.activeTabIndex);
                structuralChangePending_ = true;
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeTabs)
        {
            events::ui::RemoveUITabsComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UITabsDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UITabsHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Tabs");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUITabs", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UITabsDrawer::drawTabBarPosition(services::UITabsData& data)
    {
        bool changed = false;

        const char* positions[] = {"Top", "Bottom", "Left", "Right"};
        int currentPos = static_cast<int>(data.tabBarPosition);
        if (currentPos < 0 || currentPos > 3) currentPos = 0;

        if (ImGui::Combo("Tab Bar Position##UITabs", &currentPos, positions, 4))
        {
            data.tabBarPosition = static_cast<uint8_t>(currentPos);
            changed = true;
        }

        return changed;
    }

    bool UITabsDrawer::drawActiveTabSelector(services::UITabsData& data, int paneCount)
    {
        bool changed = false;

        if (paneCount <= 0)
        {
            // No panes yet (e.g. a bare UITabs component): keep a plain numeric field.
            int value = data.activeTabIndex;
            if (ImGui::InputInt("Active Tab##UITabs", &value))
            {
                if (value < 0) value = 0;
                data.activeTabIndex = value;
                changed = true;
            }
            return changed;
        }

        int active = data.activeTabIndex;
        if (active < 0) active = 0;
        if (active >= paneCount) active = paneCount - 1;

        std::string preview = "Tab " + std::to_string(active + 1);
        if (ImGui::BeginCombo("Active Tab##UITabs", preview.c_str()))
        {
            for (int i = 0; i < paneCount; ++i)
            {
                std::string itemLabel = "Tab " + std::to_string(i + 1);
                bool selected = (i == active);
                if (ImGui::Selectable(itemLabel.c_str(), selected) && i != data.activeTabIndex)
                {
                    data.activeTabIndex = i;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Normalize a stored out-of-range index once.
        if (!changed && data.activeTabIndex != active)
        {
            data.activeTabIndex = active;
            changed = true;
        }

        return changed;
    }

    bool UITabsDrawer::drawTabAuthoring(services::EntityHandle handle, int paneCount)
    {
        bool structural = false;

        ImGui::Separator();
        ImGui::TextDisabled("Tabs: %d", paneCount);

        if (ImGui::Button("+ Add Tab##UITabs"))
        {
            addTab(handle);
            structural = true;
        }
        ImGui::SameLine();

        const bool noTabs = (paneCount <= 0);
        if (noTabs) ImGui::BeginDisabled();
        if (ImGui::Button("- Remove Tab##UITabs"))
        {
            removeLastTab(handle);
            structural = true;
        }
        if (noTabs) ImGui::EndDisabled();

        if (structural)
            structuralChangePending_ = true;

        return structural;
    }

    void UITabsDrawer::drawCurrentState(const services::UITabsData& data)
    {
        if (ImGui::TreeNodeEx("Runtime State##UITabs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Previous Tab Index: %d", data.previousTabIndex);
            ImGui::TreePop();
        }
    }
}
