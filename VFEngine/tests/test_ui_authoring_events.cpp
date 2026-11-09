#include <doctest.h>
#include <components/Components.hpp>
#include <scene/EntityRegistry.hpp>
#include <impl/components/UIComponentService.hpp>
#include <data/EntityConversion.hpp>
#include <data/UIAuthoringData.hpp>
#include <events/EventDispatcher.hpp>
#include <events/ui/UIEvents.hpp>
#include <entt/entt.hpp>
#include <algorithm>
#include <string>
#include <vector>

// VK-1659: the UI authoring events of the MCP UI tools (events/ui/UIAuthoringEvents.hpp), against the real
// UIComponentService on the global registry: SimulateUIClickCommand (the runtime hit-test checks, the button
// notification order, the checkbox toggle rule with radio groups and label routing), IsUIListItemQuery,
// IsInUIPreviewSandboxQuery, GetUICompoundStatusQuery, and the Add Component > UI Button default label colour. Each test registers its
// own service and clears the dispatcher before and after, so no handler outlives it.

namespace
{
    struct DispatcherScope
    {
        DispatcherScope() { events::EventDispatcher::instance().clear(); }
        ~DispatcherScope() { events::EventDispatcher::instance().clear(); }
    };

    void attachChild(entt::registry& registry, entt::entity parent, entt::entity child)
    {
        registry.emplace_or_replace<components::ParentComponent>(child).parent = parent;
        auto* children = registry.try_get<components::ChildrenComponent>(parent);
        if (!children)
        {
            children = &registry.emplace<components::ChildrenComponent>(parent);
        }
        children->children.push_back(child);
    }

    void destroySubtree(entt::registry& registry, entt::entity entity)
    {
        if (!registry.valid(entity)) return;
        if (auto* children = registry.try_get<components::ChildrenComponent>(entity))
        {
            auto copy = children->children;
            for (entt::entity child : copy)
            {
                destroySubtree(registry, child);
            }
        }
        if (auto* parentComp = registry.try_get<components::ParentComponent>(entity))
        {
            if (registry.valid(parentComp->parent))
            {
                if (auto* siblings = registry.try_get<components::ChildrenComponent>(parentComp->parent))
                {
                    std::erase(siblings->children, entity);
                }
            }
        }
        registry.destroy(entity);
    }

    // One UIComponentService on the global dispatcher for one test. The entities a test makes through it and
    // the modal state are removed again at the end.
    struct AuthoringFixture
    {
        DispatcherScope dispatcherScope;
        services::UIComponentService service{nullptr};
        entt::registry& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> roots;

        AuthoringFixture()
        {
            registry.ctx().erase<components::UIModalState>();
            service.registerEventHandlers(events::EventDispatcher::instance());
        }

        ~AuthoringFixture()
        {
            for (entt::entity root : roots)
            {
                destroySubtree(registry, root);
            }
            registry.ctx().erase<components::UIModalState>();
        }

        AuthoringFixture(const AuthoringFixture&) = delete;
        AuthoringFixture& operator=(const AuthoringFixture&) = delete;

        entt::entity make(const std::string& name, entt::entity parent = entt::null)
        {
            entt::entity entity = registry.create();
            auto& nameComp = registry.emplace<components::NameComponent>(entity);
            nameComp.name = name;
            nameComp.isActive = true;
            if (parent == entt::null)
            {
                roots.push_back(entity);
            }
            else
            {
                attachChild(registry, parent, entity);
            }
            return entity;
        }

        entt::entity canvas(const std::string& name = "Canvas")
        {
            entt::entity entity = make(name);
            registry.emplace<components::UICanvasComponent>(entity);
            registry.emplace<components::UIRectComponent>(entity);
            return entity;
        }

        entt::entity button(entt::entity parent, const std::string& name)
        {
            entt::entity entity = make(name, parent);
            registry.emplace<components::UIRectComponent>(entity);
            registry.emplace<components::UIButtonComponent>(entity);
            return entity;
        }

        entt::entity checkbox(entt::entity parent, const std::string& name, const std::string& group = {},
                              bool checked = false)
        {
            entt::entity entity = make(name, parent);
            registry.emplace<components::UIRectComponent>(entity);
            auto& comp = registry.emplace<components::UICheckboxComponent>(entity);
            comp.groupName = group;
            comp.isChecked = checked;
            return entity;
        }

        services::UIClickResult click(entt::entity entity)
        {
            events::ui::SimulateUIClickCommand command;
            command.entity = services::internal::toHandle(entity);
            return events::EventDispatcher::instance().execute(command);
        }

        services::UIClickResult click(services::EntityHandle handle)
        {
            events::ui::SimulateUIClickCommand command;
            command.entity = handle;
            return events::EventDispatcher::instance().execute(command);
        }
    };

    // Records every button and checkbox notification as "<kind>:<name>[:<checked>]", in publish order.
    struct NotificationLog
    {
        std::vector<std::string> entries;

        NotificationLog()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.subscribe<events::ui::UIButtonPressedNotification>(
                [this](const events::ui::UIButtonPressedNotification& n) { entries.push_back("pressed:" + n.entityName); });
            dispatcher.subscribe<events::ui::UIButtonReleasedNotification>(
                [this](const events::ui::UIButtonReleasedNotification& n) { entries.push_back("released:" + n.entityName); });
            dispatcher.subscribe<events::ui::UIButtonClickedNotification>(
                [this](const events::ui::UIButtonClickedNotification& n) { entries.push_back("clicked:" + n.entityName); });
            dispatcher.subscribe<events::ui::UIButtonHoverEnterNotification>(
                [this](const events::ui::UIButtonHoverEnterNotification& n) { entries.push_back("hover:" + n.entityName); });
            dispatcher.subscribe<events::ui::UICheckboxToggledNotification>(
                [this](const events::ui::UICheckboxToggledNotification& n)
                {
                    entries.push_back("toggled:" + n.entityName + ":" + (n.newCheckedState ? "on" : "off") + ":" +
                                      (n.previousCheckedState ? "on" : "off"));
                });
        }
    };
}

TEST_SUITE("UIAuthoringEvents")
{
    TEST_CASE("a button click publishes Pressed, Released, Clicked with the entity name")
    {
        AuthoringFixture f;
        NotificationLog log;  // after f: destroyed first, before f clears the dispatcher

        entt::entity canvas = f.canvas();
        entt::entity pause = f.button(canvas, "PauseButton");

        const services::UIClickResult result = f.click(pause);
        CHECK(result.status == services::UIClickStatus::Clicked);
        CHECK(result.kind == services::UIClickKind::Button);
        CHECK(result.target == services::internal::toHandle(pause));
        CHECK(result.targetName == "PauseButton");
        CHECK(result.toggles.empty());
        CHECK(log.entries == std::vector<std::string>{"pressed:PauseButton", "released:PauseButton",
                                                      "clicked:PauseButton"});
    }

    TEST_CASE("refusals publish nothing and name the reason")
    {
        AuthoringFixture f;
        NotificationLog log;

        entt::entity canvas = f.canvas();

        SUBCASE("NotFound")
        {
            CHECK(f.click(services::EntityHandle::invalid()).status == services::UIClickStatus::NotFound);
            entt::entity gone = f.registry.create();
            const services::EntityHandle goneHandle = services::internal::toHandle(gone);
            f.registry.destroy(gone);
            CHECK(f.click(goneHandle).status == services::UIClickStatus::NotFound);
        }
        SUBCASE("NotClickable: no button, checkbox or toggle label")
        {
            entt::entity panel = f.make("Panel", canvas);
            f.registry.emplace<components::UIRectComponent>(panel);
            const services::UIClickResult result = f.click(panel);
            CHECK(result.status == services::UIClickStatus::NotClickable);
            CHECK(result.kind == services::UIClickKind::None);
        }
        SUBCASE("NoRect")
        {
            entt::entity bare = f.make("Bare", canvas);
            f.registry.emplace<components::UIButtonComponent>(bare);
            CHECK(f.click(bare).status == services::UIClickStatus::NoRect);
        }
        SUBCASE("NotInteractable")
        {
            entt::entity button = f.button(canvas, "Locked");
            f.registry.get<components::UIButtonComponent>(button).interactable = false;
            CHECK(f.click(button).status == services::UIClickStatus::NotInteractable);

            entt::entity box = f.checkbox(canvas, "LockedBox");
            f.registry.get<components::UICheckboxComponent>(box).interactable = false;
            CHECK(f.click(box).status == services::UIClickStatus::NotInteractable);
            CHECK_FALSE(f.registry.get<components::UICheckboxComponent>(box).isChecked);
        }
        SUBCASE("Inactive: an inactive ancestor")
        {
            entt::entity button = f.button(canvas, "Hidden");
            f.registry.get<components::NameComponent>(canvas).isActive = false;
            CHECK(f.click(button).status == services::UIClickStatus::Inactive);
        }
        SUBCASE("NoCanvas")
        {
            entt::entity loose = f.make("NotUI");
            entt::entity button = f.button(loose, "Orphan");
            CHECK(f.click(button).status == services::UIClickStatus::NoCanvas);
        }
        SUBCASE("BlockedByModal, but clickable inside the modal")
        {
            entt::entity outside = f.button(canvas, "Outside");
            entt::entity window = f.make("Dialog", canvas);
            f.registry.emplace<components::UIRectComponent>(window);
            f.registry.emplace<components::UIWindowComponent>(window).modal = true;
            entt::entity inside = f.button(window, "Inside");
            f.registry.ctx().emplace<components::UIModalState>().modalStack.push_back(window);

            CHECK(f.click(outside).status == services::UIClickStatus::BlockedByModal);
            CHECK(log.entries.empty());
            CHECK(f.click(inside).status == services::UIClickStatus::Clicked);
            CHECK(log.entries.size() == 3);
            log.entries.clear();
        }

        CHECK(log.entries.empty());
    }

    TEST_CASE("an independent checkbox toggles and reports the change")
    {
        AuthoringFixture f;
        NotificationLog log;

        entt::entity canvas = f.canvas();
        entt::entity box = f.checkbox(canvas, "Music");

        services::UIClickResult result = f.click(box);
        REQUIRE(result.status == services::UIClickStatus::Clicked);
        CHECK(result.kind == services::UIClickKind::Checkbox);
        CHECK(f.registry.get<components::UICheckboxComponent>(box).isChecked);
        REQUIRE(result.toggles.size() == 1);
        CHECK(result.toggles[0].entity == services::internal::toHandle(box));
        CHECK(result.toggles[0].name == "Music");
        CHECK(result.toggles[0].newChecked);
        CHECK_FALSE(result.toggles[0].previousChecked);
        CHECK(log.entries == std::vector<std::string>{"toggled:Music:on:off"});

        result = f.click(box);
        CHECK_FALSE(f.registry.get<components::UICheckboxComponent>(box).isChecked);
        REQUIRE(result.toggles.size() == 1);
        CHECK_FALSE(result.toggles[0].newChecked);
        CHECK(result.toggles[0].previousChecked);
    }

    TEST_CASE("a radio checkbox unchecks its checked peers first, then reports itself")
    {
        AuthoringFixture f;
        NotificationLog log;

        const std::string group = "vk1659-difficulty";
        entt::entity canvas = f.canvas();
        entt::entity easy = f.checkbox(canvas, "Easy", group, true);
        entt::entity normal = f.checkbox(canvas, "Normal", group);
        entt::entity hard = f.checkbox(canvas, "Hard", group);
        entt::entity other = f.checkbox(canvas, "OtherGroup", "vk1659-other", true);

        const services::UIClickResult result = f.click(hard);
        REQUIRE(result.status == services::UIClickStatus::Clicked);
        REQUIRE(result.toggles.size() == 2);
        CHECK(result.toggles[0].name == "Easy");
        CHECK_FALSE(result.toggles[0].newChecked);
        CHECK(result.toggles[0].previousChecked);
        CHECK(result.toggles[1].name == "Hard");
        CHECK(result.toggles[1].newChecked);
        CHECK(log.entries == std::vector<std::string>{"toggled:Easy:off:on", "toggled:Hard:on:off"});

        CHECK_FALSE(f.registry.get<components::UICheckboxComponent>(easy).isChecked);
        CHECK_FALSE(f.registry.get<components::UICheckboxComponent>(normal).isChecked);
        CHECK(f.registry.get<components::UICheckboxComponent>(hard).isChecked);
        CHECK(f.registry.get<components::UICheckboxComponent>(other).isChecked);

        SUBCASE("a checked strict radio box stays checked")
        {
            log.entries.clear();
            f.registry.get<components::UICheckboxComponent>(hard).allowUncheck = false;
            const services::UIClickResult again = f.click(hard);
            CHECK(again.status == services::UIClickStatus::Clicked);
            CHECK(again.toggles.empty());
            CHECK(log.entries.empty());
            CHECK(f.registry.get<components::UICheckboxComponent>(hard).isChecked);
        }
        SUBCASE("allowUncheck unchecks a checked radio box")
        {
            log.entries.clear();
            f.registry.get<components::UICheckboxComponent>(hard).allowUncheck = true;
            const services::UIClickResult again = f.click(hard);
            REQUIRE(again.toggles.size() == 1);
            CHECK_FALSE(again.toggles[0].newChecked);
            CHECK_FALSE(f.registry.get<components::UICheckboxComponent>(hard).isChecked);
            CHECK(log.entries == std::vector<std::string>{"toggled:Hard:off:on"});
        }
    }

    TEST_CASE("the label of a labelToggle checkbox clicks that checkbox")
    {
        AuthoringFixture f;
        NotificationLog log;

        entt::entity canvas = f.canvas();
        entt::entity box = f.checkbox(canvas, "Subtitles");
        entt::entity label = f.make("SubtitlesLabel", box);
        f.registry.emplace<components::UIRectComponent>(label);
        f.registry.emplace<components::UILabelComponent>(label);

        SUBCASE("labelToggle on")
        {
            f.registry.get<components::UICheckboxComponent>(box).labelToggle = true;
            const services::UIClickResult result = f.click(label);
            REQUIRE(result.status == services::UIClickStatus::Clicked);
            CHECK(result.kind == services::UIClickKind::Checkbox);
            CHECK(result.target == services::internal::toHandle(box));
            CHECK(result.targetName == "Subtitles");
            CHECK(f.registry.get<components::UICheckboxComponent>(box).isChecked);
            CHECK(log.entries == std::vector<std::string>{"toggled:Subtitles:on:off"});
        }
        SUBCASE("labelToggle off")
        {
            CHECK(f.click(label).status == services::UIClickStatus::NotClickable);
            CHECK_FALSE(f.registry.get<components::UICheckboxComponent>(box).isChecked);
        }
        SUBCASE("a label without a rect is never hit")
        {
            f.registry.get<components::UICheckboxComponent>(box).labelToggle = true;
            f.registry.remove<components::UIRectComponent>(label);
            CHECK(f.click(label).status == services::UIClickStatus::NoRect);
            CHECK_FALSE(f.registry.get<components::UICheckboxComponent>(box).isChecked);
        }
    }

    TEST_CASE("IsUIListItemQuery flags engine-managed list items only")
    {
        AuthoringFixture f;
        auto& dispatcher = events::EventDispatcher::instance();

        entt::entity canvas = f.canvas();
        entt::entity item = f.make("Item", canvas);
        f.registry.emplace<components::UIListItemComponent>(item);
        entt::entity authored = f.make("Authored", canvas);

        events::ui::IsUIListItemQuery query;
        query.entity = services::internal::toHandle(item);
        CHECK(dispatcher.query(query));
        query.entity = services::internal::toHandle(authored);
        CHECK_FALSE(dispatcher.query(query));
        query.entity = services::EntityHandle::invalid();
        CHECK_FALSE(dispatcher.query(query));
    }

    TEST_CASE("IsInUIPreviewSandboxQuery covers a tagged builder canvas and everything below it")
    {
        AuthoringFixture f;
        auto& dispatcher = events::EventDispatcher::instance();

        entt::entity sandbox = f.canvas("Builder Layer");
        entt::entity panel = f.make("Panel", sandbox);
        entt::entity deep = f.button(panel, "Deep");
        entt::entity scene = f.canvas("Scene Canvas");
        entt::entity sceneButton = f.button(scene, "Scene Button");

        events::ui::MarkUIPreviewSandboxCommand mark;
        mark.entity = services::internal::toHandle(sandbox);
        REQUIRE(dispatcher.execute(mark));

        events::ui::IsInUIPreviewSandboxQuery query;
        for (entt::entity inside : {sandbox, panel, deep})
        {
            query.entity = services::internal::toHandle(inside);
            CHECK(dispatcher.query(query));
        }
        for (entt::entity outside : {scene, sceneButton})
        {
            query.entity = services::internal::toHandle(outside);
            CHECK_FALSE(dispatcher.query(query));
        }
        query.entity = services::EntityHandle::invalid();
        CHECK_FALSE(dispatcher.query(query));

        mark.tagged = false;
        REQUIRE(dispatcher.execute(mark));
        query.entity = services::internal::toHandle(deep);
        CHECK_FALSE(dispatcher.query(query));
    }

    TEST_CASE("GetUICompoundStatusQuery reports the compound-widget structure checks")
    {
        AuthoringFixture f;
        auto& dispatcher = events::EventDispatcher::instance();

        entt::entity canvas = f.canvas();
        entt::entity dropdown = f.make("Dropdown", canvas);
        f.registry.emplace<components::UIRectComponent>(dropdown);
        f.registry.emplace<components::UIDropdownComponent>(dropdown);  // no options

        events::ui::GetUICompoundStatusQuery query;
        query.entity = services::internal::toHandle(dropdown);
        services::UICompoundStatusData status = dispatcher.query(query);
        CHECK_FALSE(status.ok);
        REQUIRE_FALSE(status.errors.empty());
        CHECK(status.errors[0].find("Dropdown") != std::string::npos);

        f.registry.get<components::UIDropdownComponent>(dropdown).options.push_back({"Easy", {}});
        status = dispatcher.query(query);
        CHECK(status.ok);
        CHECK(status.errors.empty());

        query.entity = services::internal::toHandle(canvas);
        status = dispatcher.query(query);
        CHECK(status.ok);
        CHECK(status.errors.empty());
        CHECK(status.warnings.empty());

        query.entity = services::EntityHandle::invalid();
        CHECK(dispatcher.query(query).ok);
    }

    TEST_CASE("ReconcileUIListViewsCommand on a tree without list views does nothing")
    {
        AuthoringFixture f;
        auto& dispatcher = events::EventDispatcher::instance();

        entt::entity canvas = f.canvas();
        f.button(canvas, "Button");

        events::ui::ReconcileUIListViewsCommand command;
        command.root = services::internal::toHandle(canvas);
        CHECK(dispatcher.execute(command) == 0);
        command.root = services::EntityHandle::invalid();
        CHECK(dispatcher.execute(command) == 0);
    }

    TEST_CASE("Add Component > UI Button gives the auto-added label dark text")
    {
        AuthoringFixture f;
        auto& dispatcher = events::EventDispatcher::instance();

        entt::entity canvas = f.canvas();
        entt::entity button = f.make("Button", canvas);

        events::ui::AddUIButtonComponentCommand add;
        add.entity = services::internal::toHandle(button);
        REQUIRE(dispatcher.execute(add));

        REQUIRE(f.registry.all_of<components::UIImageComponent, components::UILabelComponent>(button));
        const auto& label = f.registry.get<components::UILabelComponent>(button);
        CHECK(label.text == "Button");
        CHECK(label.color.r == doctest::Approx(0.196f));
        CHECK(label.color.g == doctest::Approx(0.196f));
        CHECK(label.color.b == doctest::Approx(0.196f));
        CHECK(label.color.a == doctest::Approx(1.0f));
        // The button's own state colour is unchanged: dark text on the default white button.
        CHECK(f.registry.get<components::UIButtonComponent>(button).normalColor == glm::vec4(1.0f));
    }
}
