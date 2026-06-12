#include <doctest.h>
#include <components/Components.hpp>
#include <components/ComponentClone.hpp>
#include <ui/UIRectMath.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/Entity.hpp>
#include <entt/entt.hpp>

// Modal window gating math: isInteractionAllowed (descendant walk against the
// UIModalState stack), stack ordering semantics, and the window component's
// clone reset.

namespace
{
    entt::entity makeChild(entt::registry& registry, entt::entity parent)
    {
        entt::entity child = registry.create();
        registry.emplace<components::ParentComponent>(child).parent = parent;
        auto* children = registry.try_get<components::ChildrenComponent>(parent);
        if (!children)
        {
            children = &registry.emplace<components::ChildrenComponent>(parent);
        }
        children->children.push_back(child);
        return child;
    }

    struct ModalStateGuard
    {
        entt::registry& registry;
        explicit ModalStateGuard(entt::registry& r) : registry(r)
        {
            registry.ctx().erase<components::UIModalState>();
        }
        ~ModalStateGuard()
        {
            registry.ctx().erase<components::UIModalState>();
        }
    };
}

TEST_SUITE("UIWindowModal")
{
    TEST_CASE("no modal active allows everything")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        ModalStateGuard guard(registry);

        entt::entity widget = registry.create();
        CHECK(utilities::ui::isInteractionAllowed(registry, widget));

        registry.ctx().emplace<components::UIModalState>(); // empty stack
        CHECK(utilities::ui::isInteractionAllowed(registry, widget));

        registry.destroy(widget);
    }

    TEST_CASE("active modal allows only itself and descendants")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        ModalStateGuard guard(registry);

        entt::entity modal = registry.create();
        registry.emplace<components::UIWindowComponent>(modal).modal = true;

        entt::entity insideChild = makeChild(registry, modal);
        entt::entity insideGrandchild = makeChild(registry, insideChild);
        entt::entity outside = registry.create();

        auto& state = registry.ctx().emplace<components::UIModalState>();
        state.modalStack.push_back(modal);

        CHECK(utilities::ui::isInteractionAllowed(registry, modal));
        CHECK(utilities::ui::isInteractionAllowed(registry, insideChild));
        CHECK(utilities::ui::isInteractionAllowed(registry, insideGrandchild));
        CHECK_FALSE(utilities::ui::isInteractionAllowed(registry, outside));

        registry.destroy(insideGrandchild);
        registry.destroy(insideChild);
        registry.destroy(modal);
        registry.destroy(outside);
    }

    TEST_CASE("top of the stack wins; closing it restores the one below")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        ModalStateGuard guard(registry);

        entt::entity modalA = registry.create();
        registry.emplace<components::UIWindowComponent>(modalA).modal = true;
        entt::entity childA = makeChild(registry, modalA);

        entt::entity modalB = registry.create();
        registry.emplace<components::UIWindowComponent>(modalB).modal = true;
        entt::entity childB = makeChild(registry, modalB);

        auto& state = registry.ctx().emplace<components::UIModalState>();
        state.modalStack.push_back(modalA);
        state.modalStack.push_back(modalB);

        CHECK(state.activeModal() == modalB);
        CHECK(utilities::ui::isInteractionAllowed(registry, childB));
        CHECK_FALSE(utilities::ui::isInteractionAllowed(registry, childA));

        // Close B -> A becomes the active modal
        state.modalStack.pop_back();
        CHECK(state.activeModal() == modalA);
        CHECK(utilities::ui::isInteractionAllowed(registry, childA));
        CHECK_FALSE(utilities::ui::isInteractionAllowed(registry, childB));

        registry.destroy(childB);
        registry.destroy(modalB);
        registry.destroy(childA);
        registry.destroy(modalA);
    }

    TEST_CASE("invalid modal entry fails open (does not lock the UI)")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        ModalStateGuard guard(registry);

        entt::entity modal = registry.create();
        auto& state = registry.ctx().emplace<components::UIModalState>();
        state.modalStack.push_back(modal);
        registry.destroy(modal);

        entt::entity widget = registry.create();
        CHECK(utilities::ui::isInteractionAllowed(registry, widget));
        registry.destroy(widget);
    }

    TEST_CASE("findOpenWindowAncestor resolves self and nesting")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        ModalStateGuard guard(registry);

        entt::entity window = registry.create();
        registry.emplace<components::UIWindowComponent>(window);
        entt::entity child = makeChild(registry, window);
        entt::entity orphan = registry.create();

        CHECK(utilities::ui::findOpenWindowAncestor(registry, window) == window);
        CHECK(utilities::ui::findOpenWindowAncestor(registry, child) == window);
        // Compare outside CHECK: doctest's expression decomposition is
        // ambiguous against entt::null_t's operator== overloads.
        bool orphanHasNoWindow = utilities::ui::findOpenWindowAncestor(registry, orphan) == entt::null;
        CHECK(orphanHasNoWindow);

        registry.destroy(child);
        registry.destroy(window);
        registry.destroy(orphan);
    }

    TEST_CASE("clone resets window drag/hover runtime state")
    {
        scene::Entity src("WindowCloneSrc");
        scene::Entity dst("WindowCloneDst");

        auto& window = src.addComponent<components::UIWindowComponent>();
        window.title = "Settings";
        window.modal = true;
        window.isDraggingWindow = true;            // runtime - must reset
        window.dragStartMousePos = {50.0f, 60.0f}; // runtime - must reset
        window.closeHovered = true;                // runtime - must reset

        components::cloneOptionalComponents(src, dst);

        REQUIRE(dst.hasComponent<components::UIWindowComponent>());
        const auto& cloned = dst.getComponent<components::UIWindowComponent>();
        CHECK(cloned.title == "Settings");
        CHECK(cloned.modal);
        CHECK_FALSE(cloned.isDraggingWindow);
        CHECK(cloned.dragStartMousePos.x == doctest::Approx(0.0f));
        CHECK_FALSE(cloned.closeHovered);
    }
}
