#include <doctest.h>

// VK-1435 — UI Layer Builder isolation guarantees (CPU-only).
//
// The builder authors into the live singleton registry (the UI runtime is ECS-bound to it),
// so isolation from the user's scene is achieved by a marker component, UIPreviewTagComponent,
// NOT a separate registry. Two invariants make that safe and are unit-testable here:
//
//   1. The scene serializer SKIPS a tagged subtree, so a builder sandbox can never leak into
//      a saved scene. This is the load-bearing guarantee — it mirrors the existing
//      UIListItemComponent skip (see test_ui_listview.cpp). We assert it at BOTH skip sites:
//      the serial path (serializeEntity) and the parallel path (saveScene -> serializeRootEntity).
//
//   2. controllers::offscreen::ui_common::isEffectivelyActiveWithin lets the offscreen preview
//      render a subtree whose own root is intentionally inactive (so the main UI passes skip it)
//      while still honoring the active flags of the subtree's interior. The walk up the parent
//      chain stops at (and treats as active) the scope root.

#include <components/Components.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <asset/AssetDatabase.hpp>

// Header-only inline scoped active-check under test. Lives in graphics, but depends only on
// utilities + glm + entt + the services EntityConversion header, all of which are on the Tests
// project include path (VFEngine/graphics, VFEngine/utilities, VFEngine/services).
#include <controllers/offscreen/UICommon.hpp>
#include <controllers/offscreen/UIInteractionSystem.hpp>
#include <controllers/offscreen/FramePreparationSystem.hpp>

#include <nlohmann/json.hpp>
#include <entt/entt.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    void attachChild(entt::registry& registry, entt::entity parent, entt::entity child)
    {
        registry.emplace_or_replace<components::ParentComponent>(child).parent = parent;
        auto* children = registry.try_get<components::ChildrenComponent>(parent);
        if (!children)
            children = &registry.emplace<components::ChildrenComponent>(parent);
        children->children.push_back(child);
    }

    void destroySubtree(entt::registry& registry, entt::entity entity)
    {
        if (!registry.valid(entity)) return;
        if (auto* children = registry.try_get<components::ChildrenComponent>(entity))
        {
            auto copy = children->children;
            for (entt::entity child : copy)
                destroySubtree(registry, child);
        }
        if (auto* parentComp = registry.try_get<components::ParentComponent>(entity))
        {
            if (registry.valid(parentComp->parent))
            {
                if (auto* siblings = registry.try_get<components::ChildrenComponent>(parentComp->parent))
                    std::erase(siblings->children, entity);
            }
        }
        registry.destroy(entity);
    }

    // Depth-first search for a child object with the given "name" within a serialized
    // entity-JSON node's "children" array (one level — used to assert sibling presence).
    bool hasNamedChild(const json& node, const std::string& name)
    {
        if (!node.contains("children") || !node["children"].is_array())
            return false;
        for (const auto& child : node["children"])
        {
            if (child.value("name", std::string{}) == name)
                return true;
        }
        return false;
    }

    // Recursively collect every "name" appearing anywhere in an entity-JSON subtree.
    void collectNames(const json& node, std::vector<std::string>& out)
    {
        if (node.contains("name") && node["name"].is_string())
            out.push_back(node["name"].get<std::string>());
        if (node.contains("children") && node["children"].is_array())
            for (const auto& child : node["children"])
                collectNames(child, out);
    }

    bool containsName(const std::vector<std::string>& names, const std::string& name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    fs::path isolationTestRoot()
    {
        return fs::temp_directory_path() / "vf_ui_preview_isolation_tests";
    }
}

TEST_SUITE("UIPreviewIsolation")
{
    // -------- 1. The KEY guarantee: a tagged subtree is excluded from serialization --------

    TEST_CASE("serializeEntity (serial path) omits a UIPreviewTagComponent subtree, keeps the sibling")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // sceneRoot
        //   ├── RealCanvas (untagged)            <- must be serialized
        //   │     └── RealChild
        //   └── PreviewCanvas (tagged)           <- must be skipped, with ALL descendants
        //         └── PreviewChild
        scene::Entity sceneRoot("SceneRoot");
        scene::Entity realCanvas("RealCanvas");
        scene::Entity realChild("RealChild");
        scene::Entity previewCanvas("PreviewCanvas");
        scene::Entity previewChild("PreviewChild");

        attachChild(registry, sceneRoot.getHandle(), realCanvas.getHandle());
        attachChild(registry, realCanvas.getHandle(), realChild.getHandle());
        attachChild(registry, sceneRoot.getHandle(), previewCanvas.getHandle());
        attachChild(registry, previewCanvas.getHandle(), previewChild.getHandle());

        // Tag the sandbox canvas root.
        registry.emplace<components::UIPreviewTagComponent>(previewCanvas.getHandle());

        json out = serialization::SceneSerialization::serializeEntity(sceneRoot);

        std::vector<std::string> names;
        collectNames(out, names);

        // The untagged branch survives in full.
        CHECK(hasNamedChild(out, "RealCanvas"));
        CHECK(containsName(names, "RealCanvas"));
        CHECK(containsName(names, "RealChild"));

        // The tagged branch AND its descendants are absent.
        CHECK_FALSE(hasNamedChild(out, "PreviewCanvas"));
        CHECK_FALSE(containsName(names, "PreviewCanvas"));
        CHECK_FALSE(containsName(names, "PreviewChild"));

        destroySubtree(registry, sceneRoot.getHandle());
    }

    TEST_CASE("saveScene (parallel root path) omits a UIPreviewTagComponent subtree from the file")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::error_code ec;
        fs::remove_all(isolationTestRoot(), ec);
        fs::create_directories(isolationTestRoot(), ec);
        asset::AssetDatabase::instance().clear();

        // serializeRootEntity (parallelChildren=true) is private and only reachable through
        // saveScene/createSnapshot. Build the tree under the scene-graph root so the parallel
        // per-top-level-child branch (the OTHER skip site) is exercised, then read the file.
        scene::SceneGraphSystem scene;
        scene::Entity& root = scene.GetRoot();

        // Two top-level children so serializeRootEntity takes the parallel branch
        // (children.size() > 1): one real, one tagged-preview.
        scene::Entity realTop("RealTop");
        scene::Entity realLeaf("RealLeaf");
        scene::Entity previewTop("PreviewTop");
        scene::Entity previewLeaf("PreviewLeaf");

        attachChild(registry, root.getHandle(), realTop.getHandle());
        attachChild(registry, realTop.getHandle(), realLeaf.getHandle());
        attachChild(registry, root.getHandle(), previewTop.getHandle());
        attachChild(registry, previewTop.getHandle(), previewLeaf.getHandle());

        registry.emplace<components::UIPreviewTagComponent>(previewTop.getHandle());

        fs::path scenePath = isolationTestRoot() / "IsolationScene.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(scene, scenePath.string()));

        std::ifstream file(scenePath);
        REQUIRE(file.is_open());
        json sceneJson;
        file >> sceneJson;
        REQUIRE(sceneJson.contains("root"));

        std::vector<std::string> names;
        collectNames(sceneJson["root"], names);

        // Real top-level subtree persisted.
        CHECK(containsName(names, "RealTop"));
        CHECK(containsName(names, "RealLeaf"));

        // Tagged preview subtree skipped at the parallel root branch.
        CHECK_FALSE(containsName(names, "PreviewTop"));
        CHECK_FALSE(containsName(names, "PreviewLeaf"));

        // Cleanup: destroy the entities we attached under the persistent singleton root.
        destroySubtree(registry, realTop.getHandle());
        destroySubtree(registry, previewTop.getHandle());
        fs::remove_all(isolationTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    // -------- 2. isEffectivelyActiveWithin --------

    using controllers::offscreen::ui_common::isEffectivelyActiveWithin;

    TEST_CASE("isEffectivelyActiveWithin with scopeRoot==null is identical to isEffectivelyActive")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // root(active) -> mid(inactive) -> leaf(active)
        scene::Entity root("AR_Root");
        scene::Entity mid("AR_Mid");
        scene::Entity leaf("AR_Leaf");
        attachChild(registry, root.getHandle(), mid.getHandle());
        attachChild(registry, mid.getHandle(), leaf.getHandle());

        // Sweep every active-flag combination on the three nodes and demand byte-identical
        // results between the two functions for scopeRoot == entt::null.
        for (int combo = 0; combo < 8; ++combo)
        {
            registry.get<components::NameComponent>(root.getHandle()).isActive  = (combo & 1) != 0;
            registry.get<components::NameComponent>(mid.getHandle()).isActive   = (combo & 2) != 0;
            registry.get<components::NameComponent>(leaf.getHandle()).isActive  = (combo & 4) != 0;

            for (entt::entity e : {root.getHandle(), mid.getHandle(), leaf.getHandle()})
            {
                bool reference = scene::Entity::isEffectivelyActive(registry, e);
                bool scoped = isEffectivelyActiveWithin(registry, e, entt::null);
                CHECK(scoped == reference);
            }
        }

        destroySubtree(registry, root.getHandle());
    }

    TEST_CASE("isEffectivelyActiveWithin stops at scopeRoot: an inactive ancestor ABOVE it "
              "does not deactivate a descendant")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // outer(INACTIVE) -> scopeRoot(INACTIVE) -> child(active) -> grandchild(active)
        // The builder makes scopeRoot inactive on purpose (so the main passes skip it); the
        // preview must still treat scopeRoot — and everything under it — as active, even when
        // an ancestor above the scope is also inactive.
        scene::Entity outer("SC_Outer");
        scene::Entity scopeRoot("SC_ScopeRoot");
        scene::Entity child("SC_Child");
        scene::Entity grandchild("SC_Grandchild");
        attachChild(registry, outer.getHandle(), scopeRoot.getHandle());
        attachChild(registry, scopeRoot.getHandle(), child.getHandle());
        attachChild(registry, child.getHandle(), grandchild.getHandle());

        registry.get<components::NameComponent>(outer.getHandle()).isActive = false;
        registry.get<components::NameComponent>(scopeRoot.getHandle()).isActive = false;
        // child + grandchild keep the default isActive == true.

        // Sanity: the engine-wide test reports them inactive (the inactive scopeRoot/outer
        // ancestors propagate down) — that's exactly why a scoped variant is needed.
        CHECK_FALSE(scene::Entity::isEffectivelyActive(registry, child.getHandle()));
        CHECK_FALSE(scene::Entity::isEffectivelyActive(registry, grandchild.getHandle()));

        // Scoped to scopeRoot: the walk stops at scopeRoot (treated active), so the inactive
        // outer ancestor is never consulted -> descendants are active.
        CHECK(isEffectivelyActiveWithin(registry, scopeRoot.getHandle(), scopeRoot.getHandle()));
        CHECK(isEffectivelyActiveWithin(registry, child.getHandle(), scopeRoot.getHandle()));
        CHECK(isEffectivelyActiveWithin(registry, grandchild.getHandle(), scopeRoot.getHandle()));

        destroySubtree(registry, outer.getHandle());
    }

    TEST_CASE("isEffectivelyActiveWithin still honors an inactive intermediate parent BELOW scopeRoot")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // scopeRoot(inactive) -> mid(INACTIVE) -> leaf(active)
        // An inactive node BETWEEN the scope root and the leaf must still deactivate the leaf:
        // the walk reaches `mid` (and returns false) before it ever reaches scopeRoot.
        scene::Entity scopeRoot("SB_ScopeRoot");
        scene::Entity mid("SB_Mid");
        scene::Entity leaf("SB_Leaf");
        attachChild(registry, scopeRoot.getHandle(), mid.getHandle());
        attachChild(registry, mid.getHandle(), leaf.getHandle());

        registry.get<components::NameComponent>(scopeRoot.getHandle()).isActive = false;
        registry.get<components::NameComponent>(mid.getHandle()).isActive = false;
        // leaf keeps isActive == true.

        // scopeRoot itself is active within its own scope.
        CHECK(isEffectivelyActiveWithin(registry, scopeRoot.getHandle(), scopeRoot.getHandle()));
        // mid is the scope root's direct child and is inactive -> inactive within scope.
        CHECK_FALSE(isEffectivelyActiveWithin(registry, mid.getHandle(), scopeRoot.getHandle()));
        // leaf is gated by its inactive parent `mid`, encountered before scopeRoot.
        CHECK_FALSE(isEffectivelyActiveWithin(registry, leaf.getHandle(), scopeRoot.getHandle()));

        // Flip mid active: leaf now active within scope (scopeRoot's own inactive flag is ignored).
        registry.get<components::NameComponent>(mid.getHandle()).isActive = true;
        CHECK(isEffectivelyActiveWithin(registry, mid.getHandle(), scopeRoot.getHandle()));
        CHECK(isEffectivelyActiveWithin(registry, leaf.getHandle(), scopeRoot.getHandle()));

        destroySubtree(registry, scopeRoot.getHandle());
    }

    TEST_CASE("tabs scoped preview preserves pane eye toggles until active tab changes")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        scene::Entity canvas("TabsPreviewCanvas");
        scene::Entity tabsEntity("TabsWidget");
        scene::Entity tabBar("TabsTabBar");
        scene::Entity pane0("TabsPane0");
        scene::Entity pane1("TabsPane1");

        canvas.addComponent<components::UICanvasComponent>();
        auto& tabs = tabsEntity.addComponent<components::UITabsComponent>();
        tabs.activeTabIndex = 0;
        tabsEntity.addComponent<components::UIRectComponent>();
        tabBar.addComponent<components::UILayoutGroupComponent>();

        attachChild(registry, canvas.getHandle(), tabsEntity.getHandle());
        attachChild(registry, tabsEntity.getHandle(), tabBar.getHandle());
        attachChild(registry, tabsEntity.getHandle(), pane0.getHandle());
        attachChild(registry, tabsEntity.getHandle(), pane1.getHandle());

        controllers::offscreen::UIInteractionSystem interaction;
        controllers::offscreen::FrameContext ctx;
        ctx.editPreview = true;

        interaction.applyTabsActivePaneScoped(ctx, canvas.getHandle());
        CHECK(registry.get<components::NameComponent>(pane0.getHandle()).isActive);
        CHECK_FALSE(registry.get<components::NameComponent>(pane1.getHandle()).isActive);

        registry.get<components::NameComponent>(pane0.getHandle()).isActive = false;
        registry.get<components::NameComponent>(pane1.getHandle()).isActive = true;
        interaction.applyTabsActivePaneScoped(ctx, canvas.getHandle());
        CHECK_FALSE(registry.get<components::NameComponent>(pane0.getHandle()).isActive);
        CHECK(registry.get<components::NameComponent>(pane1.getHandle()).isActive);

        tabs.activeTabIndex = 1;
        interaction.applyTabsActivePaneScoped(ctx, canvas.getHandle());
        CHECK_FALSE(registry.get<components::NameComponent>(pane0.getHandle()).isActive);
        CHECK(registry.get<components::NameComponent>(pane1.getHandle()).isActive);

        tabs.activeTabIndex = 0;
        interaction.applyTabsActivePaneScoped(ctx, canvas.getHandle());
        CHECK(registry.get<components::NameComponent>(pane0.getHandle()).isActive);
        CHECK_FALSE(registry.get<components::NameComponent>(pane1.getHandle()).isActive);

        destroySubtree(registry, canvas.getHandle());
    }
}
