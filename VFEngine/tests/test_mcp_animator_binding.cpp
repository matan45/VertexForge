#include "test_mcp_animation_fixture.hpp"

#include "tools/AnimationToolSupport.hpp"
#include "tools/ToolHelpers.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// VK-1656: the "Animator" built-in component binding of component_add / _get / _set / _remove. It presents
// the Mesh's animatorRef / retargetRef / applyRootMotion as a component of its own, creates a one-state
// .vfAnimator from creation seeds, refuses its read-only fields and replays undo snapshots without them.
namespace
{
    using mcpanim::contains;
    namespace fs = std::filesystem;
    namespace anim = mcp::tools::anim;

    const std::vector<std::string> heroBones{"Hips", "Spine", "Head", "LeftArm"};
    const std::vector<int32_t> heroParents{-1, 0, 1, 1};

    // Entities: 1 "Hero" (skinned mesh), 2 "Crate" (static mesh), 3 "Empty" (no Mesh).
    struct BindingFixture : mcpanim::AnimationFixture
    {
        asset::AssetRef heroMesh;
        asset::AssetRef idleClip;
        asset::AssetRef runClip;

        BindingFixture()
            : mcpanim::AnimationFixture("VertexForge_McpAnimatorBinding")
        {
            heroMesh = engine.addMesh("characters/hero/Hero.vfMesh", heroBones, heroParents);
            const asset::AssetRef crateMesh = engine.addMesh("props/Crate.vfMesh", {}, {});
            idleClip = engine.addClip("characters/hero/Hero_Idle.vfAnim", heroBones);
            runClip = engine.addClip("characters/hero/Hero_Run.vfAnim", heroBones);
            engine.addEntity(1, "Hero", heroMesh);
            engine.addEntity(2, "Crate", crateMesh);
            engine.addEntity(3, "Empty");
        }

        // An existing two-state animator (Idle -> Run on "speed").
        asset::AssetRef addLocomotion(const std::string& relative)
        {
            animator::AnimatorData data = anim::newAnimator("Loco", "Idle", idleClip, 1.0f, true);
            animator::AnimatorState run;
            run.id = data.graph.nextStateId++;
            run.name = "Run";
            run.animationRef = runClip;
            data.graph.states.push_back(run);
            data.graph.parameters.push_back({"speed", animator::AnimatorParameterType::Float, 0.0f});
            return engine.addAnimator(relative, std::move(data));
        }
    };
}

TEST_SUITE("McpAnimatorBinding")
{
    TEST_CASE("component_add creates a one-state animator next to the mesh from the seeds")
    {
        BindingFixture fixture;

        const nlohmann::json result = fixture.callOk("component_add", {
            {"entity", 1}, {"type", "Animator"},
            {"data", {{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"playbackSpeed", 1.25}, {"loop", false}}}
        });

        mcpanim::StoredAnimator* created = fixture.engine.animatorAt("characters/hero/Hero.vfAnimator");
        REQUIRE(created != nullptr);
        const animator::AnimatorData& data = created->data;
        CHECK(data.name == "Hero");
        CHECK(data.layers.empty());
        REQUIRE(data.graph.states.size() == 1);
        const animator::AnimatorState& state = data.graph.states[0];
        CHECK(state.id == 1);
        CHECK(state.name == "Idle");
        CHECK(state.animationRef == fixture.idleClip);
        CHECK(state.playbackSpeed == 1.25f);
        CHECK_FALSE(state.loop);
        CHECK(state.position == glm::vec2(300.0f, 100.0f));
        CHECK(data.graph.defaultStateId == 1);
        CHECK(data.graph.nextStateId == 2);
        CHECK(data.graph.transitions.empty());

        const services::MeshData& mesh = fixture.engine.meshOf(1);
        REQUIRE(mesh.animatorRef.isValid());
        CHECK(mcpanim::pathKey(mesh.animatorRef.resolve()) ==
              mcpanim::pathKey(fixture.engine.file("characters/hero/Hero.vfAnimator").string()));
        CHECK(mesh.meshRef == fixture.heroMesh);

        const nlohmann::json& fields = result["data"];
        CHECK(fields["defaultState"] == "Idle");
        CHECK(fields["states"] == nlohmann::json::array({"Idle"}));
        CHECK(fields["playbackSpeed"] == 1.25);
        CHECK(fields["loop"] == false);
        CHECK(fields["layers"] == 1);
        CHECK(fields["parameters"] == nlohmann::json::array());
        CHECK(contains(fields["defaultClip"].get<std::string>(), "Hero_Idle.vfAnim"));
        CHECK(fields["skeleton"]["bones"] == 4);
        CHECK(fields["skeleton"]["root"] == "Hips");
        CHECK(contains(fields["skeleton"]["mesh"].get<std::string>(), "Hero.vfMesh"));
        CHECK(fields["retargetRef"] == "");
        CHECK(fields["applyRootMotion"] == false);

        // Creating the file is not undoable: the one entry restores the assignment only.
        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(mcpanim::batchSize(fixture.engine.undoStack[0]) == 1);
        CHECK(fixture.engine.undoStack[0]->getDescription() == "MCP: Add Animator component");
        CHECK(fixture.engine.log == std::vector<std::string>{"SaveAnimator:Hero.vfAnimator", "SetMeshData:1", "PushUndo"});
    }

    TEST_CASE("component_add picks a free name, or creates the animator at data.animatorRef")
    {
        BindingFixture fixture;

        SUBCASE("next to the mesh, skipping a taken name")
        {
            mcpanim::touch(fixture.engine.file("characters/hero/Hero.vfAnimator"));
            fixture.callOk("component_add", {{"entity", 1}, {"type", "Animator"},
                                             {"data", {{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}}}});
            REQUIRE(fixture.engine.animatorAt("characters/hero/Hero_1.vfAnimator") != nullptr);
            CHECK(contains(fixture.engine.meshOf(1).animatorRef.resolve(), "Hero_1.vfAnimator"));
        }

        SUBCASE("at an explicit new path, with the default seeds")
        {
            fixture.callOk("component_add", {{"entity", 1}, {"type", "Animator"},
                                             {"data", {{"animatorRef", "anims/HeroLoco.vfAnimator"},
                                                       {"defaultClip", "characters/hero/Hero_Run.vfAnim"},
                                                       {"defaultState", "Run"}}}});
            mcpanim::StoredAnimator* created = fixture.engine.animatorAt("anims/HeroLoco.vfAnimator");
            REQUIRE(created != nullptr);
            REQUIRE(created->data.graph.states.size() == 1);
            CHECK(created->data.graph.states[0].name == "Run");
            CHECK(created->data.graph.states[0].playbackSpeed == 1.0f);
            CHECK(created->data.graph.states[0].loop);
            CHECK(created->data.name == "HeroLoco");
            CHECK(contains(fixture.engine.meshOf(1).animatorRef.resolve(), "HeroLoco.vfAnimator"));
        }
    }

    TEST_CASE("component_add assigns an existing animator and refuses seeds for it")
    {
        BindingFixture fixture;
        const asset::AssetRef loco = fixture.addLocomotion("anims/Loco.vfAnimator");

        const std::string seeded = fixture.argError("component_add", {
            {"entity", 1}, {"type", "Animator"},
            {"data", {{"animatorRef", "anims/Loco.vfAnimator"}, {"defaultClip", "characters/hero/Hero_Idle.vfAnim"}}}
        });
        CHECK(contains(seeded, "apply only when component_add creates the animator"));
        CHECK(contains(seeded, "animation_set_state_machine"));
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());
        CHECK_FALSE(fixture.engine.logged("SaveAnimator"));
        CHECK(fixture.engine.undoStack.empty());

        const nlohmann::json result = fixture.callOk("component_add", {
            {"entity", 1}, {"type", "Animator"}, {"data", {{"animatorRef", "anims/Loco.vfAnimator"}, {"applyRootMotion", true}}}
        });
        CHECK(fixture.engine.meshOf(1).animatorRef == loco);
        CHECK(fixture.engine.meshOf(1).applyRootMotion);
        CHECK(result["data"]["states"] == nlohmann::json::array({"Idle", "Run"}));
        CHECK(result["data"]["parameters"] == nlohmann::json::array({{{"name", "speed"}, {"type", "Float"}}}));
        CHECK_FALSE(fixture.engine.logged("SaveAnimator"));
        CHECK(fixture.engine.undoStack.size() == 1);
    }

    TEST_CASE("component_add validates everything before it creates a file")
    {
        BindingFixture fixture;
        const auto add = [&fixture](const nlohmann::json& data)
        {
            return fixture.argError("component_add", {{"entity", 1}, {"type", "Animator"}, {"data", data}});
        };

        CHECK(contains(add(nlohmann::json::object()), "needs data.defaultClip"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Missing.vfAnim"}}), "no clip at"));
        CHECK(contains(add({{"defaultClip", "../outside.vfAnim"}}), "outside the allowed root"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"defaultState", "Any"}}), "reserved"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"playbackSpeed", 0}}),
                       "greater than 0"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"loop", "yes"}}), "boolean"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"states", {"Idle"}}}), "read-only"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"speed", 1}}),
                       "unknown Animator field 'speed'"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"retargetRef", "rigs/Missing.vfretarget"}}),
                       "does not exist"));
        CHECK(contains(add({{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}, {"animatorRef", "anims/Hero.vfMesh"}}),
                       ".vfAnimator"));

        CHECK_FALSE(fixture.engine.logged("SaveAnimator"));
        CHECK(fixture.engine.animators.empty());
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());
        CHECK(fixture.engine.undoStack.empty());
    }

    TEST_CASE("component_add needs a skinned mesh inside the project")
    {
        BindingFixture fixture;
        const nlohmann::json seeds{{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}};

        const std::string staticMesh = fixture.failure("component_add", {{"entity", 2}, {"type", "Animator"}, {"data", seeds}});
        CHECK(contains(staticMesh, "AddAnimatorComponent failed"));
        CHECK(contains(staticMesh, "skinned"));
        CHECK(contains(fixture.failure("component_add", {{"entity", 3}, {"type", "Animator"}, {"data", seeds}}),
                       "needs a Mesh component"));

        // A mesh outside the project: no folder to create the animator in, unless one is named.
        const fs::path outside = mcpanim::makeTestDirectory("VertexForge_McpAnimatorBinding_Outside");
        const asset::AssetRef foreign = fixture.engine.addMeshAt(outside / "Guard.vfMesh", heroBones, heroParents);
        fixture.engine.addEntity(4, "Guard", foreign);
        const std::string error = fixture.argError("component_add", {{"entity", 4}, {"type", "Animator"}, {"data", seeds}});
        CHECK(contains(error, "lies outside the project"));
        CHECK(contains(error, "data.animatorRef"));
        fixture.callOk("component_add", {{"entity", 4}, {"type", "Animator"},
                                         {"data", {{"defaultClip", "characters/hero/Hero_Idle.vfAnim"},
                                                   {"animatorRef", "anims/Guard.vfAnimator"}}}});
        CHECK(fixture.engine.animatorAt("anims/Guard.vfAnimator") != nullptr);

        std::error_code ec;
        fs::remove_all(outside, ec);
    }

    TEST_CASE("creating an animator is refused in Play mode and during a scene load; assigning one is not")
    {
        BindingFixture fixture;
        fixture.addLocomotion("anims/Loco.vfAnimator");
        const nlohmann::json seeds{{"defaultClip", "characters/hero/Hero_Idle.vfAnim"}};

        fixture.engine.playMode = true;
        CHECK(contains(fixture.failure("component_add", {{"entity", 1}, {"type", "Animator"}, {"data", seeds}}),
                       "Play mode"));
        fixture.engine.playMode = false;
        fixture.engine.loadPending = true;
        CHECK(contains(fixture.failure("component_add", {{"entity", 1}, {"type", "Animator"}, {"data", seeds}}),
                       "scene load"));
        CHECK_FALSE(fixture.engine.logged("SaveAnimator"));
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());

        fixture.engine.loadPending = false;
        fixture.engine.playMode = true;
        fixture.callOk("component_add", {{"entity", 1}, {"type", "Animator"},
                                         {"data", {{"animatorRef", "anims/Loco.vfAnimator"}}}});
        CHECK(fixture.engine.meshOf(1).animatorRef.isValid());
    }

    TEST_CASE("component_set refuses the read-only fields and an empty animatorRef")
    {
        BindingFixture fixture;
        const asset::AssetRef loco = fixture.addLocomotion("anims/Loco.vfAnimator");
        fixture.engine.meshOf(1).animatorRef = loco;

        const std::string readOnly = fixture.argError("component_set", {{"entity", 1}, {"type", "Animator"},
                                                                         {"data", {{"defaultState", "Run"}}}});
        CHECK(contains(readOnly, "field 'defaultState' of Animator is read-only"));
        CHECK(contains(readOnly, "animatorRef, retargetRef, applyRootMotion"));
        CHECK(contains(readOnly, "animation_set_state_machine"));
        CHECK(contains(fixture.argError("component_set", {{"entity", 1}, {"type", "Animator"},
                                                          {"data", {{"skeleton", nullptr}}}}), "read-only"));
        CHECK(contains(fixture.argError("component_set", {{"entity", 1}, {"type", "Animator"},
                                                          {"data", {{"speed", 2}}}}), "valid fields"));
        CHECK(contains(fixture.argError("component_set", {{"entity", 1}, {"type", "Animator"},
                                                          {"data", {{"animatorRef", ""}}}}), "component_remove"));
        CHECK(contains(fixture.argError("component_set", {{"entity", 1}, {"type", "Animator"},
                                                          {"data", {{"animatorRef", "characters/hero/Hero.vfMesh"}}}}),
                       ".vfAnimator"));
        CHECK_FALSE(fixture.engine.logged("SetMeshData"));
        CHECK(fixture.engine.undoStack.empty());

        // An entity without an animator has no Animator component to set.
        CHECK(fixture.call("component_set", {{"entity", 2}, {"type", "Animator"},
                                             {"data", {{"applyRootMotion", true}}}}).isError);
    }

    TEST_CASE("component_get reports every key even when the animator file cannot be read")
    {
        BindingFixture fixture;
        // Registered and on disk, but not a readable animator.
        fixture.engine.meshOf(1).animatorRef = fixture.engine.addFile("anims/Broken.vfAnimator");

        const nlohmann::json result = fixture.callOk("component_get", {{"entity", 1}, {"type", "Animator"}});
        const nlohmann::json& fields = result["data"];
        for (const char* key : {"animatorRef", "retargetRef", "applyRootMotion", "skeleton", "defaultState", "defaultClip",
                                "playbackSpeed", "loop", "states", "parameters", "layers"})
        {
            CAPTURE(key);
            CHECK(fields.contains(key));
        }
        CHECK(fields["defaultState"].is_null());
        CHECK(fields["states"].is_null());
        CHECK(fields["layers"].is_null());
        CHECK(fields["skeleton"]["bones"] == 4);

        // No animator: no component.
        CHECK(fixture.call("component_get", {{"entity", 2}, {"type", "Animator"}}).isError);
    }

    TEST_CASE("component_remove clears animatorRef, retargetRef and applyRootMotion; undo restores them")
    {
        BindingFixture fixture;
        const asset::AssetRef loco = fixture.addLocomotion("anims/Loco.vfAnimator");
        const asset::AssetRef retarget = fixture.engine.addFile("rigs/MixamoToHero.vfretarget");
        services::MeshData& mesh = fixture.engine.meshOf(1);
        mesh.animatorRef = loco;
        mesh.retargetRef = retarget;
        mesh.applyRootMotion = true;
        mesh.renderLayer = 3;

        fixture.callOk("component_remove", {{"entity", 1}, {"type", "Animator"}});
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());
        CHECK_FALSE(fixture.engine.meshOf(1).retargetRef.isValid());
        CHECK_FALSE(fixture.engine.meshOf(1).applyRootMotion);
        CHECK(fixture.engine.meshOf(1).meshRef == fixture.heroMesh); // the rest of the Mesh stays
        CHECK(fixture.engine.meshOf(1).renderLayer == 3u);
        REQUIRE(fixture.engine.undoStack.size() == 1);

        fixture.engine.undoStack[0]->undo();
        CHECK(fixture.engine.meshOf(1).animatorRef == loco);
        CHECK(fixture.engine.meshOf(1).retargetRef == retarget);
        CHECK(fixture.engine.meshOf(1).applyRootMotion);

        fixture.engine.undoStack[0]->execute();
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());
        CHECK_FALSE(fixture.engine.meshOf(1).retargetRef.isValid());

        // The file itself is untouched.
        CHECK(fixture.engine.animatorAt("anims/Loco.vfAnimator") != nullptr);
    }

    TEST_CASE("removing an absent Animator changes nothing, so a failed component_add keeps a retargetRef")
    {
        BindingFixture fixture;
        fixture.addLocomotion("anims/Loco.vfAnimator");
        const asset::AssetRef retarget = fixture.engine.addFile("rigs/MixamoToHero.vfretarget");
        fixture.engine.meshOf(1).retargetRef = retarget;

        CHECK(mcp::tools::removeBuiltinComponent("Animator", mcpanim::handleOf(1)));
        CHECK(fixture.engine.meshOf(1).retargetRef == retarget);
        CHECK_FALSE(fixture.engine.logged("SetMeshData"));

        // The add fails after add() succeeded: the rollback must leave the Mesh as it was.
        CHECK(contains(fixture.argError("component_add", {{"entity", 1}, {"type", "Animator"},
                                                          {"data", {{"animatorRef", "anims/Loco.vfAnimator"},
                                                                    {"applyRootMotion", "yes"}}}}), "boolean"));
        CHECK(fixture.engine.meshOf(1).retargetRef == retarget);
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());
        CHECK_FALSE(fixture.engine.logged("SetMeshData"));
    }

    TEST_CASE("undoing an added Animator restores the Mesh fields it lives in, so an earlier retargetRef stays")
    {
        BindingFixture fixture;
        const asset::AssetRef loco = fixture.addLocomotion("anims/Loco.vfAnimator");
        const asset::AssetRef retarget = fixture.engine.addFile("rigs/MixamoToHero.vfretarget");
        fixture.engine.meshOf(1).retargetRef = retarget; // set on the Mesh before any animator

        fixture.callOk("component_add", {{"entity", 1}, {"type", "Animator"},
                                         {"data", {{"animatorRef", "anims/Loco.vfAnimator"}, {"applyRootMotion", true}}}});
        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(mcpanim::batchSize(fixture.engine.undoStack[0]) == 1);
        CHECK(fixture.engine.undoStack[0]->getDescription() == "MCP: Add Animator component");

        fixture.engine.undoStack[0]->undo();
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());
        CHECK(fixture.engine.meshOf(1).retargetRef == retarget);
        CHECK_FALSE(fixture.engine.meshOf(1).applyRootMotion);

        fixture.engine.undoStack[0]->execute();
        CHECK(fixture.engine.meshOf(1).animatorRef == loco);
        CHECK(fixture.engine.meshOf(1).retargetRef == retarget);
        CHECK(fixture.engine.meshOf(1).applyRootMotion);
    }

    TEST_CASE("component_add / component_set undo and redo replay snapshots that include the read-only fields")
    {
        BindingFixture fixture;
        const asset::AssetRef loco = fixture.addLocomotion("anims/Loco.vfAnimator");

        fixture.callOk("component_add", {{"entity", 1}, {"type", "Animator"}, {"data", {{"animatorRef", "anims/Loco.vfAnimator"}}}});
        fixture.callOk("component_set", {{"entity", 1}, {"type", "Animator"}, {"data", {{"applyRootMotion", true}}}});
        REQUIRE(fixture.engine.undoStack.size() == 2);
        CHECK(fixture.engine.meshOf(1).applyRootMotion);

        fixture.engine.undoStack[1]->undo(); // the set
        CHECK_FALSE(fixture.engine.meshOf(1).applyRootMotion);
        CHECK(fixture.engine.meshOf(1).animatorRef == loco);

        fixture.engine.undoStack[0]->undo(); // the add
        CHECK_FALSE(fixture.engine.meshOf(1).animatorRef.isValid());

        fixture.engine.undoStack[0]->execute(); // redo the add: presence comes back through patch(animatorRef)
        CHECK(fixture.engine.meshOf(1).animatorRef == loco);
        CHECK_FALSE(fixture.engine.meshOf(1).applyRootMotion);

        fixture.engine.undoStack[1]->execute(); // redo the set
        CHECK(fixture.engine.meshOf(1).applyRootMotion);

        // The same replay through the free functions the undo entries use.
        const std::optional<nlohmann::json> snapshot = mcp::tools::getBuiltinComponent("Animator", mcpanim::handleOf(1));
        REQUIRE(snapshot.has_value());
        CHECK(snapshot->contains("states"));
        CHECK_NOTHROW(mcp::tools::patchBuiltinComponent("Animator", mcpanim::handleOf(1), *snapshot));
    }
}
