#include <doctest.h>
#include <components/Components.hpp>
#include <components/ComponentClone.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <entt/entt.hpp>

// VK-1343: entity duplication must copy UI and other registered components (not just a fixed
// hand-maintained list), while resetting runtime-only state. These tests exercise the
// centralized clone facility directly (HierarchyService::duplicateEntity additionally needs the
// event dispatcher / scene graph, which the CPU-only Tests project does not link).

TEST_SUITE("EntityDuplication")
{
    TEST_CASE("cloneOptionalComponents copies UI + data components and resets runtime state")
    {
        scene::Entity src("DuplicationSource");
        scene::Entity dst("DuplicationDest");

        auto& rect = src.addComponent<components::UIRectComponent>();
        rect.anchorMin = {0.1f, 0.2f};
        rect.sizeDelta = {300.0f, 120.0f};
        rect.anchoredPosition = {5.0f, 6.0f};

        auto& label = src.addComponent<components::UILabelComponent>();
        label.text = "Hello";
        label.fontSize = 42.0f;
        label.color = {0.2f, 0.4f, 0.6f, 1.0f};

        auto& btn = src.addComponent<components::UIButtonComponent>();
        btn.normalColor = {0.3f, 0.3f, 0.3f, 1.0f};
        btn.interactable = false;
        btn.currentState = components::UIButtonState::Pressed;     // runtime - must reset
        btn.currentDisplayColor = {0.9f, 0.1f, 0.1f, 1.0f};        // runtime - must reset

        auto& img = src.addComponent<components::UIImageComponent>();
        img.colorTint = {0.5f, 0.5f, 0.5f, 1.0f};
        img.renderTextureSourceName = "RT_Source";                // authored - must keep
        img.renderTextureSource = static_cast<entt::entity>(123); // runtime - must reset

        // A non-UI optional component to prove coverage beyond UI.
        auto& light = src.addComponent<components::PointLightComponent>();
        light.intensity = 7.5f;
        light.radius = 12.0f;

        components::cloneOptionalComponents(src, dst);

        // Presence
        CHECK(dst.hasComponent<components::UIRectComponent>());
        CHECK(dst.hasComponent<components::UILabelComponent>());
        CHECK(dst.hasComponent<components::UIButtonComponent>());
        CHECK(dst.hasComponent<components::UIImageComponent>());
        CHECK(dst.hasComponent<components::PointLightComponent>());

        // Authored config copied
        CHECK(dst.getComponent<components::UIRectComponent>().sizeDelta.x == doctest::Approx(300.0f));
        CHECK(dst.getComponent<components::UILabelComponent>().text == "Hello");
        CHECK(dst.getComponent<components::UILabelComponent>().fontSize == doctest::Approx(42.0f));
        CHECK_FALSE(dst.getComponent<components::UIButtonComponent>().interactable);
        CHECK(dst.getComponent<components::UIImageComponent>().renderTextureSourceName == "RT_Source");
        CHECK(dst.getComponent<components::PointLightComponent>().intensity == doctest::Approx(7.5f));
        CHECK(dst.getComponent<components::PointLightComponent>().radius == doctest::Approx(12.0f));

        // Runtime-only fields reset
        const auto& dBtn = dst.getComponent<components::UIButtonComponent>();
        CHECK(dBtn.currentState == components::UIButtonState::Normal);
        CHECK(dBtn.currentDisplayColor.r == doctest::Approx(dBtn.normalColor.r));
        CHECK(dBtn.currentDisplayColor.g == doctest::Approx(dBtn.normalColor.g));
        CHECK(dBtn.currentDisplayColor.b == doctest::Approx(dBtn.normalColor.b));
        // Compare outside CHECK: doctest's expression decomposition is ambiguous against
        // entt's operator==(entity, null_t).
        const bool renderTextureSourceReset =
            dst.getComponent<components::UIImageComponent>().renderTextureSource == entt::null;
        CHECK(renderTextureSourceReset);

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }

    TEST_CASE("cloneOptionalComponents resets slider, progress bar and animation runtime state")
    {
        scene::Entity src("RuntimeStateSource");
        scene::Entity dst("RuntimeStateDest");

        auto& slider = src.addComponent<components::UISliderComponent>();
        slider.value = 0.75f;
        slider.currentState = components::UISliderState::Pressed; // runtime
        slider.isDragging = true;                                 // runtime
        slider.dragStartValue = 0.4f;                             // runtime

        auto& bar = src.addComponent<components::UIProgressBarComponent>();
        bar.value = 1.0f;
        bar.maxValue = 1.0f;
        bar.displayValue = 0.2f;    // runtime - should snap to value
        bar.completedFired = false; // runtime - should become value >= maxValue

        auto& anim = src.addComponent<components::UIAnimationComponent>();
        anim.autoPlay = true;
        anim.isPlaying = true;   // runtime
        anim.elapsedTime = 3.0f; // runtime

        components::cloneOptionalComponents(src, dst);

        const auto& dSlider = dst.getComponent<components::UISliderComponent>();
        CHECK(dSlider.value == doctest::Approx(0.75f)); // authored kept
        CHECK(dSlider.currentState == components::UISliderState::Normal);
        CHECK_FALSE(dSlider.isDragging);
        CHECK(dSlider.dragStartValue == doctest::Approx(0.0f));

        const auto& dBar = dst.getComponent<components::UIProgressBarComponent>();
        CHECK(dBar.displayValue == doctest::Approx(1.0f));
        CHECK(dBar.completedFired);

        const auto& dAnim = dst.getComponent<components::UIAnimationComponent>();
        CHECK(dAnim.autoPlay); // authored kept
        CHECK_FALSE(dAnim.isPlaying);
        CHECK(dAnim.elapsedTime == doctest::Approx(0.0f));

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }

    TEST_CASE("cloneOptionalComponents does not value-copy ScriptComponent")
    {
        scene::Entity src("ScriptSource");
        scene::Entity dst("ScriptDest");

        src.addComponent<components::ScriptComponent>();

        components::cloneOptionalComponents(src, dst);

        // Scripts are reattached by the caller via the scripting system, not value-copied here.
        CHECK_FALSE(dst.hasComponent<components::ScriptComponent>());

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }
}
