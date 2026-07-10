#include "doctest.h"

#include "data/PluginTextureTypes.hpp"
#include "render/ui/UITextureKeys.hpp"
#include "components/UIComponents.hpp"
#include "components/ComponentClone.hpp"
#include "data/DTOs.hpp"

// VK-1488 — generic "bind a plugin/GPU texture to a UIImage" API. CPU-only coverage:
// deterministic key formatting, the synthetic-key predicate (which guards the
// "__white_1x1__" pitfall), the DTO<->component field, and the clone-reset of the
// runtime-only key. The PluginTextureManager registry itself needs a Vulkan device,
// so it is exercised in-engine, not here.

TEST_CASE("VK-1488: pluginTextureUIKey is __plugintex_<id>__")
{
    CHECK(plugin::pluginTextureUIKey(plugin::PluginTextureHandle{0}) == "__plugintex_0__");
    CHECK(plugin::pluginTextureUIKey(plugin::PluginTextureHandle{1}) == "__plugintex_1__");
    CHECK(plugin::pluginTextureUIKey(plugin::PluginTextureHandle{42}) == "__plugintex_42__");
    CHECK(plugin::pluginTextureUIKey(plugin::PluginTextureHandle{123456789ULL}) == "__plugintex_123456789__");
}

TEST_CASE("VK-1488: isSyntheticUITextureKey classifies external keys, not the white default")
{
    using render::ui::isSyntheticUITextureKey;

    // RTT + plugin textures live in the external-texture cache and frame-skip when unregistered.
    CHECK(isSyntheticUITextureKey("__rtt_5__"));
    CHECK(isSyntheticUITextureKey("__plugintex_7__"));

    // The default white texture is a real cached texture at bindless index 0. It must NOT
    // be classified synthetic, or every plain colored quad would frame-skip and vanish.
    CHECK_FALSE(isSyntheticUITextureKey("__white_1x1__"));
    CHECK_FALSE(isSyntheticUITextureKey("assets/ui/hud/portrait.vfImage"));
    CHECK_FALSE(isSyntheticUITextureKey(""));
}

TEST_CASE("VK-1488: UIImageData carries the external texture key")
{
    services::UIImageData data;
    CHECK(data.externalTextureKey.empty());

    data.externalTextureKey = "__plugintex_9__";
    services::UIImageData copy = data;
    CHECK(copy.externalTextureKey == "__plugintex_9__");
}

TEST_CASE("VK-1488: cloning a UIImageComponent drops the runtime plugin-texture binding")
{
    components::UIImageComponent comp;
    comp.externalTextureKey = "__plugintex_3__";
    comp.renderTextureSource = static_cast<entt::entity>(17);

    // A clone must not inherit a live binding to a texture the plugin registered for the
    // source entity (the id is process-lifetime and would dangle / mis-point).
    components::resetClonedRuntimeState<components::UIImageComponent>(comp);

    CHECK(comp.externalTextureKey.empty());
    // Extra parens: stop doctest decomposing the ==, which collides with entt's
    // custom operator==(entity, null_t) and is otherwise ambiguous.
    CHECK((comp.renderTextureSource == entt::null));
}
