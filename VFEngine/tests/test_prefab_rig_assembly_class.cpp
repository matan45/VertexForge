// VK-1433 Layer A — PrefabRigAssembly CLASS-level coverage (asset-free paths only).
//
// test_prefab_rig_assembly.cpp covers the pure prefabrig:: free functions. This file exercises
// the PrefabRigAssembly *class* itself for the paths that need NO disk assets and NO Vulkan:
//   * default-constructed accessor safety (every accessor returns a safe empty / identity)
//   * out-of-range accessor safety after a build (never dangles / crashes)
//   * a static-only build() (bogus mesh paths -> AnimationDataCache returns null gracefully,
//     so each part is a valid static part; no animator, no skeleton, no GPU)
//   * setRootModelMatrix() propagation through a static attachment chain in update()
//   * pause()/play()/isPaused() flag toggling
//
// build()/update() with REAL skeletal parts (AnimationLayerStack, IK solve over loaded bones)
// needs on-disk .vfMesh/.vfAnimator assets and is out of scope here — that path is covered by
// the pure-math free-function tests plus in-editor GPU verification (Layer B).
//
// NOTE: a static-only build() on non-existent mesh paths logs "[AnimationDataCache] Failed to
// open mesh ..." errors. That is expected and harmless — loadSkeleton/loadSockets return null
// for a missing file (they never throw), and build() still produces valid static parts.

#include <doctest.h>

#include "controllers/preview/PrefabRigAssembly.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using controllers::PrefabRigAssembly;
using controllers::PrefabRigDesc;
using controllers::PrefabRigPart;

namespace
{
    glm::vec3 translationOf(const glm::mat4& m) { return glm::vec3(m[3]); }

    // A static part with a deliberately non-existent mesh path: loadSkeleton/loadSockets
    // return null (no throw), so the assembly classifies it as a renderable static part.
    PrefabRigPart staticPart(const std::string& mesh, int parentIndex = -1,
                             const std::string& parentSocket = "")
    {
        PrefabRigPart p;
        p.meshPath = mesh;            // intentionally bogus -> no skeleton/animator loaded
        p.animatorPath.clear();       // static
        p.parentPartIndex = parentIndex;
        p.attachParentSocket = parentSocket;
        return p;
    }
}

TEST_SUITE("PrefabRigAssemblyClass")
{
    // -----------------------------------------------------------------------
    // A fresh (un-built) assembly: every accessor is safe and returns empties.
    // -----------------------------------------------------------------------
    TEST_CASE("a default-constructed assembly reports not-built and safe empty accessors")
    {
        PrefabRigAssembly rig;

        CHECK_FALSE(rig.isBuilt());
        CHECK(rig.partCount() == 0);

        // Accessors on an empty rig must not dangle/crash.
        CHECK(rig.boneMatrices(0).empty());
        CHECK(rig.partWorld(0) == glm::mat4(1.0f));
        CHECK(rig.meshPath(0).empty());
        CHECK_FALSE(rig.isSkeletalPart(0));
        CHECK(rig.editableSockets(0).empty());
        CHECK(rig.editableChains().empty());
        CHECK(rig.animatorData(0) == nullptr);

        // No-op state controls on an empty rig must not crash.
        CHECK_FALSE(rig.forceState(0, "Idle"));
        rig.setBool(0, "b", true);
        rig.setFloat(0, "f", 1.0f);
        rig.setInt(0, "i", 1);
        rig.setTrigger(0, "t");

        // update() is a no-op when not built (must not crash).
        rig.update(0.016f);
        CHECK_FALSE(rig.isBuilt());
    }

    // -----------------------------------------------------------------------
    // pause()/play()/isPaused() — pure flag, no build required.
    // -----------------------------------------------------------------------
    TEST_CASE("pause/play toggles isPaused")
    {
        PrefabRigAssembly rig;

        CHECK_FALSE(rig.isPaused()); // defaults to playing
        rig.pause();
        CHECK(rig.isPaused());
        rig.play();
        CHECK_FALSE(rig.isPaused());
    }

    // -----------------------------------------------------------------------
    // setRootModelMatrix is a faithful pass-through (rootModelMatrix_() reads it back).
    // -----------------------------------------------------------------------
    TEST_CASE("setRootModelMatrix is stored and read back verbatim")
    {
        PrefabRigAssembly rig;
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -2.0f, 5.0f));
        rig.setRootModelMatrix(m);
        CHECK(rig.rootModelMatrix_() == m);
    }

    // -----------------------------------------------------------------------
    // A static-only build() succeeds (a static-only desc is a valid build) and
    // each part is a non-skeletal part with an identity bone set.
    // -----------------------------------------------------------------------
    TEST_CASE("a static-only build yields valid non-skeletal parts")
    {
        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_no_such_root.vfMesh"));
        desc.parts.push_back(staticPart("__vk1433_no_such_child.vfMesh"));

        PrefabRigAssembly rig;
        const bool ok = rig.build(desc);

        CHECK(ok);
        CHECK(rig.isBuilt());
        REQUIRE(rig.partCount() == 2);

        for (size_t i = 0; i < rig.partCount(); ++i)
        {
            CHECK_FALSE(rig.isSkeletalPart(i));
            // Static parts feed a single identity bone matrix to the skinned pipeline.
            REQUIRE(rig.boneMatrices(i).size() == 1);
            CHECK(rig.boneMatrices(i)[0] == glm::mat4(1.0f));
        }
        CHECK(rig.meshPath(0) == "__vk1433_no_such_root.vfMesh");
        CHECK(rig.meshPath(1) == "__vk1433_no_such_child.vfMesh");
        // No animator was requested, so there are no editable chains.
        CHECK(rig.editableChains().empty());
    }

    // -----------------------------------------------------------------------
    // Out-of-range accessors after a real (static) build return safe empties.
    // -----------------------------------------------------------------------
    TEST_CASE("out-of-range accessors on a built assembly return safe empties")
    {
        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_oor.vfMesh"));

        PrefabRigAssembly rig;
        REQUIRE(rig.build(desc));
        REQUIRE(rig.partCount() == 1);

        const size_t big = 999;
        CHECK(rig.boneMatrices(big).empty());
        CHECK(rig.partWorld(big) == glm::mat4(1.0f));
        CHECK(rig.meshPath(big).empty());
        CHECK_FALSE(rig.isSkeletalPart(big));
        CHECK(rig.editableSockets(big).empty());
        CHECK(rig.animatorData(big) == nullptr);
        CHECK_FALSE(rig.forceState(big, "Idle"));

        // The same emptySockets backing object is returned for any OOR index (no per-call alloc).
        CHECK(&rig.editableSockets(big) == &rig.editableSockets(big + 1));
    }

    // -----------------------------------------------------------------------
    // setRootModelMatrix propagates to the root part's world through update(),
    // and on through a static attachment chain (parent-before-child topo order).
    // Static parts need no assets, so this exercises the real update() chain.
    // -----------------------------------------------------------------------
    TEST_CASE("setRootModelMatrix propagates to the root and through a static child in update")
    {
        // Root(static) <- Child(static, attached to Root). Child has no resolvable socket on
        // the (skeleton-less) parent, so parentSocketModel stays identity and the child rides
        // the parent's world directly (composeChildWorld with identity rot/scale).
        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_chain_root.vfMesh"));
        desc.parts.push_back(staticPart("__vk1433_chain_child.vfMesh",
                                        /*parentIndex*/ 0, /*parentSocket*/ "Mount"));

        PrefabRigAssembly rig;
        REQUIRE(rig.build(desc));
        REQUIRE(rig.partCount() == 2);

        const glm::mat4 root = glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 0.0f, -4.0f));
        rig.setRootModelMatrix(root);
        rig.update(0.0f); // dt irrelevant for static parts

        // Root part world == the supplied root model matrix.
        CHECK(translationOf(rig.partWorld(0)).x == doctest::Approx(7.0f));
        CHECK(translationOf(rig.partWorld(0)).z == doctest::Approx(-4.0f));

        // Child world inherits the root translation (identity socket offset, identity attach
        // rotation/scale): the child rides the parent's origin.
        CHECK(translationOf(rig.partWorld(1)).x == doctest::Approx(7.0f));
        CHECK(translationOf(rig.partWorld(1)).z == doctest::Approx(-4.0f));
    }

    // -----------------------------------------------------------------------
    // Re-building from a different desc replaces the previous parts (build() clears first).
    // -----------------------------------------------------------------------
    TEST_CASE("rebuilding replaces the previous parts")
    {
        PrefabRigAssembly rig;

        PrefabRigDesc first;
        first.parts.push_back(staticPart("__vk1433_a.vfMesh"));
        first.parts.push_back(staticPart("__vk1433_b.vfMesh"));
        REQUIRE(rig.build(first));
        REQUIRE(rig.partCount() == 2);

        PrefabRigDesc second;
        second.parts.push_back(staticPart("__vk1433_c.vfMesh"));
        REQUIRE(rig.build(second));
        REQUIRE(rig.partCount() == 1);
        CHECK(rig.meshPath(0) == "__vk1433_c.vfMesh");
    }

    // -----------------------------------------------------------------------
    // An empty desc builds to zero parts and reports not-built (build returns false).
    // -----------------------------------------------------------------------
    TEST_CASE("an empty desc produces no parts and reports not built")
    {
        PrefabRigDesc desc; // no parts

        PrefabRigAssembly rig;
        const bool ok = rig.build(desc);

        CHECK_FALSE(ok);            // built = !parts.empty() -> false
        CHECK_FALSE(rig.isBuilt());
        CHECK(rig.partCount() == 0);
        rig.update(0.016f);        // still a no-op, no crash
    }

    // -----------------------------------------------------------------------
    // VK-1433 — preview transform on a PARENT propagates to a child's partWorld.
    // The root's preview transform pre-multiplies into its partWorld and the child
    // composes off the parent's partWorld, so editing the root moves the child too.
    // -----------------------------------------------------------------------
    TEST_CASE("root preview transform affects all parts (propagates to a static child)")
    {
        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_pt_root.vfMesh"));
        desc.parts.push_back(staticPart("__vk1433_pt_child.vfMesh",
                                        /*parentIndex*/ 0, /*parentSocket*/ "Mount"));

        PrefabRigAssembly rig;
        REQUIRE(rig.build(desc));
        REQUIRE(rig.partCount() == 2);

        // Root preview = translate(+10,0,0). rootModelMatrix stays identity (default).
        rig.setPartPreviewTransform(0, glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)));
        rig.update(0.0f);

        // Root world == rootModelMatrix(identity) * preview => translation (10,0,0).
        CHECK(translationOf(rig.partWorld(0)).x == doctest::Approx(10.0f));
        // Child rides the parent's world (identity socket + identity child preview) => same (10,0,0).
        CHECK(translationOf(rig.partWorld(1)).x == doctest::Approx(10.0f));
    }

    // -----------------------------------------------------------------------
    // VK-1433 — a CHILD's own preview transform stacks on top of the inherited
    // parent world (parent preview propagates; child preview adds in local space).
    // -----------------------------------------------------------------------
    TEST_CASE("child preview transform stacks on top of the inherited parent world")
    {
        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_pt2_root.vfMesh"));
        desc.parts.push_back(staticPart("__vk1433_pt2_child.vfMesh",
                                        /*parentIndex*/ 0, /*parentSocket*/ "Mount"));

        PrefabRigAssembly rig;
        REQUIRE(rig.build(desc));

        rig.setPartPreviewTransform(0, glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)));
        rig.setPartPreviewTransform(1, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)));
        rig.update(0.0f);

        // Child world = parentWorld(10,0,0) * childPreview(0,5,0) => (10,5,0).
        CHECK(translationOf(rig.partWorld(1)).x == doctest::Approx(10.0f));
        CHECK(translationOf(rig.partWorld(1)).y == doctest::Approx(5.0f));
        // Parent itself is unaffected by the child's preview.
        CHECK(translationOf(rig.partWorld(0)).y == doctest::Approx(0.0f));
    }

    // -----------------------------------------------------------------------
    // VK-1433 — setPartPreviewTransform / partPreviewTransform round-trip + reset.
    // -----------------------------------------------------------------------
    TEST_CASE("preview transform accessor round-trips and resetPreviewTransforms clears it")
    {
        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_pt3.vfMesh"));

        PrefabRigAssembly rig;
        REQUIRE(rig.build(desc));

        const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
        rig.setPartPreviewTransform(0, m);
        CHECK(rig.partPreviewTransform(0) == m);

        rig.resetPreviewTransforms();
        CHECK(rig.partPreviewTransform(0) == glm::mat4(1.0f));

        // Out-of-range accessor returns identity and the setter is a safe no-op.
        CHECK(rig.partPreviewTransform(999) == glm::mat4(1.0f));
        rig.setPartPreviewTransform(999, m); // no crash
    }

    // -----------------------------------------------------------------------
    // VK-1433 — a rebuild resets preview transforms to identity (parts reconstructed).
    // -----------------------------------------------------------------------
    TEST_CASE("rebuild resets preview transforms to identity")
    {
        PrefabRigAssembly rig;

        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_pt4.vfMesh"));
        REQUIRE(rig.build(desc));

        rig.setPartPreviewTransform(0, glm::translate(glm::mat4(1.0f), glm::vec3(9.0f, 0.0f, 0.0f)));
        CHECK(translationOf(rig.partPreviewTransform(0)).x == doctest::Approx(9.0f));

        REQUIRE(rig.build(desc)); // rebuild from scratch
        CHECK(rig.partPreviewTransform(0) == glm::mat4(1.0f));
    }

    // -----------------------------------------------------------------------
    // VK-1433 — frame-step / seek are safe no-ops on STATIC and out-of-range parts
    // (they require an AnimationLayerStack, which static parts do not have). The
    // deterministic time-advance / clamp behavior over a real skeletal animator is
    // covered at the state-machine level (test_animator_state_machine_seek.cpp) and
    // by in-editor GPU verification (Layer B).
    // -----------------------------------------------------------------------
    TEST_CASE("stepFrame / setNormalizedTime / normalizedTime are safe on static + OOR parts")
    {
        PrefabRigDesc desc;
        desc.parts.push_back(staticPart("__vk1433_scrub_static.vfMesh"));

        PrefabRigAssembly rig;
        REQUIRE(rig.build(desc));

        // Static part has no stack: all scrub ops are no-ops, normalizedTime reads 0.
        rig.stepFrame(0, 1);
        rig.stepFrame(0, -3);
        rig.setNormalizedTime(0, 0.5f);
        CHECK(rig.normalizedTime(0) == doctest::Approx(0.0f));

        // Out-of-range indices never crash.
        rig.stepFrame(999, 1);
        rig.setNormalizedTime(999, 0.5f);
        CHECK(rig.normalizedTime(999) == doctest::Approx(0.0f));
    }
}
