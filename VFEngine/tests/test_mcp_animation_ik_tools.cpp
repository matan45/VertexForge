#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"
#include "undo/IkStateUndo.hpp"

#include "asset/AssetGUID.hpp"
#include "asset/AssetRef.hpp"
#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationAuthoringEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/physics/IKEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// VK-1656: the MCP IK tools against a fake engine holding entities, their meshes, the skeletons the
// .vfMesh files would yield, the IK components and their runtime targets. The log records every mutating
// command in dispatch order.
namespace
{
    using animator::ik::IKChainConfig;
    using animator::ik::JointConstraint;
    using animator::ik::JointConstraintType;

    constexpr uint64_t heroId = 5;      // skinned mesh with an animator
    constexpr uint64_t crateId = 6;     // static mesh
    constexpr uint64_t emptyId = 7;     // no mesh
    constexpr uint64_t mannequinId = 8; // the hero's mesh, no animator
    const std::string heroMesh = "C:/VFTestProject/characters/Hero.vfMesh";
    const std::string crateMesh = "C:/VFTestProject/props/Crate.vfMesh";

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }
    };

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    asset::AssetRef assetAt(uint64_t guid, const std::string& path)
    {
        return asset::AssetRef::fromGUIDAndPath(asset::AssetGUID::fromValue(guid), path);
    }

    IKChainConfig chainConfig(std::string name, std::string tip, std::vector<std::string> bones)
    {
        IKChainConfig chain;
        chain.chainName = std::move(name);
        chain.tipBoneName = std::move(tip);
        chain.chainBoneNames = std::move(bones);
        return chain;
    }

    // Hips -> Spine -> Chest -> {UpperArm_L -> LowerArm_L -> Hand_L, UpperArm_R -> LowerArm_R -> Hand_R};
    // Hips -> UpperLeg_L -> LowerLeg_L -> Foot_L. The .vfMesh authors one chain, LeftLeg.
    services::MeshSkeletonInfo heroSkeleton()
    {
        services::MeshSkeletonInfo info;
        info.success = true;
        info.skinned = true;
        info.boneNames = {"Hips", "Spine", "Chest", "UpperArm_L", "LowerArm_L", "Hand_L", "UpperArm_R", "LowerArm_R",
                          "Hand_R", "UpperLeg_L", "LowerLeg_L", "Foot_L"};
        info.parents = {-1, 0, 1, 2, 3, 4, 2, 6, 7, 0, 9, 10};
        info.ikChains = {chainConfig("LeftLeg", "Foot_L", {"UpperLeg_L", "LowerLeg_L", "Foot_L"})};
        return info;
    }

    std::vector<std::string> namesOf(const std::vector<IKChainConfig>& chains)
    {
        std::vector<std::string> names;
        for (const IKChainConfig& chain : chains)
        {
            names.push_back(chain.chainName);
        }
        return names;
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    bool anyContains(const nlohmann::json& warnings, const std::string& needle)
    {
        for (const nlohmann::json& warning : warnings)
        {
            if (warning.is_string() && contains(warning.get<std::string>(), needle))
            {
                return true;
            }
        }
        return false;
    }

    std::string logText(const std::vector<std::string>& log)
    {
        std::string text;
        for (const std::string& entry : log)
        {
            text += text.empty() ? entry : ", " + entry;
        }
        return "[" + text + "]";
    }

    void checkLog(const std::vector<std::string>& actual, const std::vector<std::string>& expected)
    {
        INFO("expected " << logText(expected));
        INFO("actual   " << logText(actual));
        CHECK(actual == expected);
    }

    struct FakeIkEngine
    {
        struct Target
        {
            glm::vec3 position{0.0f};
            std::optional<glm::quat> rotation;
        };

        std::map<uint64_t, services::EntityData> entities;
        std::map<uint64_t, services::MeshData> meshes;
        std::map<std::string, services::MeshSkeletonInfo> skeletons;
        std::map<uint64_t, std::vector<IKChainConfig>> components; // an entry = the entity has an IK component
        std::map<std::pair<uint64_t, std::string>, Target> targets;
        bool playMode = false;
        bool loadPending = false;
        std::vector<std::string> log;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;

        FakeIkEngine()
        {
            addEntity(heroId, "Hero");
            addEntity(crateId, "Crate");
            addEntity(emptyId, "Empty");
            addEntity(mannequinId, "Mannequin");

            services::MeshData hero;
            hero.meshRef = assetAt(1, heroMesh);
            hero.animatorRef = assetAt(2, "C:/VFTestProject/characters/Hero.vfAnimator");
            meshes[heroId] = hero;

            services::MeshData mannequin;
            mannequin.meshRef = assetAt(1, heroMesh);
            meshes[mannequinId] = mannequin;

            services::MeshData crate;
            crate.meshRef = assetAt(3, crateMesh);
            meshes[crateId] = crate;

            skeletons[heroMesh] = heroSkeleton();
            services::MeshSkeletonInfo staticMesh;
            staticMesh.success = true;
            skeletons[crateMesh] = staticMesh;
        }

        void addEntity(uint64_t id, const std::string& name)
        {
            services::EntityData data;
            data.handle = handleOf(id);
            data.name = name;
            data.parent = handleOf(100);
            entities[id] = data;
        }

        // An undone delete re-created the entity under a new id (EntityIdRemap follows it).
        void moveEntity(uint64_t from, uint64_t to)
        {
            entities[to] = entities.at(from);
            entities[to].handle = handleOf(to);
            entities.erase(from);
            if (auto mesh = meshes.find(from); mesh != meshes.end())
            {
                meshes[to] = mesh->second;
                meshes.erase(mesh);
            }
            if (auto component = components.find(from); component != components.end())
            {
                components[to] = component->second;
                components.erase(component);
            }
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();

            d.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    const auto it = entities.find(query.entity.id);
                    return it != entities.end() ? std::optional<services::EntityData>(it->second) : std::nullopt;
                });
            d.registerQueryHandler<events::scene::GetMeshDataQuery>(
                [this](const events::scene::GetMeshDataQuery& query) -> std::optional<services::MeshData>
                {
                    const auto it = meshes.find(query.entity.id);
                    return it != meshes.end() ? std::optional<services::MeshData>(it->second) : std::nullopt;
                });
            d.registerQueryHandler<events::animationAuthoring::GetMeshSkeletonQuery>(
                [this](const events::animationAuthoring::GetMeshSkeletonQuery& query) -> services::MeshSkeletonInfo
                {
                    const auto it = skeletons.find(query.meshPath);
                    if (it != skeletons.end())
                    {
                        return it->second;
                    }
                    services::MeshSkeletonInfo missing;
                    missing.error = "cannot open " + query.meshPath;
                    return missing;
                });

            d.registerQueryHandler<events::ik::HasIKComponentQuery>(
                [this](const events::ik::HasIKComponentQuery& query) { return components.contains(query.entity.id); });
            d.registerQueryHandler<events::ik::GetIKChainConfigsQuery>(
                [this](const events::ik::GetIKChainConfigsQuery& query) -> std::vector<IKChainConfig>
                {
                    const auto it = components.find(query.entity.id);
                    return it != components.end() ? it->second : std::vector<IKChainConfig>{};
                });
            // IKAdapter::addIKComponent: refuses a second component and copies the chains of the mesh.
            d.registerCommandHandler<events::ik::AddIKComponentCommand>(
                [this](const events::ik::AddIKComponentCommand& command)
                {
                    log.push_back("AddIKComponent");
                    if (!entities.contains(command.entity.id) || components.contains(command.entity.id))
                    {
                        return false;
                    }
                    std::vector<IKChainConfig> authored;
                    if (const auto mesh = meshes.find(command.entity.id); mesh != meshes.end())
                    {
                        if (const auto skeleton = skeletons.find(mesh->second.meshRef.resolve());
                            skeleton != skeletons.end())
                        {
                            authored = skeleton->second.ikChains;
                        }
                    }
                    components[command.entity.id] = authored;
                    return true;
                });
            d.registerCommandHandler<events::ik::RemoveIKComponentCommand>(
                [this](const events::ik::RemoveIKComponentCommand& command)
                {
                    log.push_back("RemoveIKComponent");
                    return components.erase(command.entity.id) > 0;
                });
            // IKAdapter::setChains: replaces the list and drops every runtime target.
            d.registerCommandHandler<events::ik::SetIKChainsCommand>(
                [this](const events::ik::SetIKChainsCommand& command)
                {
                    log.push_back("SetIKChains");
                    const auto it = components.find(command.entity.id);
                    if (it == components.end())
                    {
                        return false;
                    }
                    it->second = command.chains;
                    std::erase_if(targets, [&command](const auto& entry) { return entry.first.first == command.entity.id; });
                    return true;
                });
            d.registerCommandHandler<events::ik::SetIKChainWeightCommand>(
                [this](const events::ik::SetIKChainWeightCommand& command)
                {
                    log.push_back("SetIKChainWeight:" + command.chainName);
                    if (const auto it = components.find(command.entity.id); it != components.end())
                    {
                        for (IKChainConfig& chain : it->second)
                        {
                            if (chain.chainName == command.chainName)
                            {
                                chain.weight = command.weight;
                            }
                        }
                    }
                });
            d.registerCommandHandler<events::ik::SetIKTargetCommand>(
                [this](const events::ik::SetIKTargetCommand& command)
                {
                    log.push_back("SetIKTarget:" + command.chainName);
                    targets[{command.entity.id, command.chainName}] = Target{command.targetPosition, command.targetRotation};
                });

            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command)
                {
                    log.push_back("PushUndo");
                    undoStack.push_back(command.command);
                });
        }
    };

    struct IkToolFixture
    {
        DispatcherScope dispatcherScope;
        FakeIkEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        IkToolFixture()
        {
            engine.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerAnimationIkTools(registry, context);
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            auto tool = registry.find(name);
            REQUIRE(tool != nullptr);
            return tool->handler(args);
        }

        nlohmann::json callOk(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        std::string argError(const char* name, const nlohmann::json& args)
        {
            try
            {
                call(name, args);
            }
            catch (const mcp::ArgError& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not an ArgError: ") + e.what() + ">";
            }
            return "<no error>";
        }

        std::string runtimeError(const char* name, const nlohmann::json& args)
        {
            try
            {
                call(name, args);
            }
            catch (const std::runtime_error& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not a runtime_error: ") + e.what() + ">";
            }
            return "<no error>";
        }

        std::vector<std::string> chainNames(uint64_t entity) const
        {
            const auto it = engine.components.find(entity);
            return it != engine.components.end() ? namesOf(it->second) : std::vector<std::string>{};
        }
    };

    nlohmann::json withChains(uint64_t entity, nlohmann::json chains)
    {
        return {{"entity", entity}, {"chains", std::move(chains)}};
    }

    nlohmann::json leftArm()
    {
        return {{"name", "LeftArm"}, {"tipBone", "Hand_L"}, {"chainLength", 3}};
    }
}

TEST_SUITE("McpAnimationIkTools")
{
    TEST_CASE("registering the IK tools dispatches nothing; four Main-affinity tools with closed schemas")
    {
        DispatcherScope scope; // no handlers at all: any dispatch would throw
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        const mcp::tools::ToolContext context{queue};
        CHECK_NOTHROW(mcp::tools::registerAnimationIkTools(registry, context));
        CHECK(registry.size() == 4);

        struct Expected
        {
            const char* name;
            bool readOnly;
            bool destructive;
        };
        for (const Expected& entry : {Expected{"animation_ik_get", true, false}, Expected{"animation_ik_set", false, true},
                                      Expected{"animation_ik_remove", false, true},
                                      Expected{"animation_ik_set_target", false, false}})
        {
            CAPTURE(entry.name);
            const auto tool = registry.find(entry.name);
            REQUIRE(tool != nullptr);
            CHECK(tool->affinity == mcp::ThreadAffinity::Main);
            CHECK(tool->readOnly == entry.readOnly);
            CHECK(tool->destructive == entry.destructive);
            CHECK(tool->inputSchema["additionalProperties"] == false);
        }

        const nlohmann::json chain = registry.find("animation_ik_set")->inputSchema["properties"]["chains"]["items"];
        CHECK(chain["additionalProperties"] == false);
        CHECK(chain["required"] == nlohmann::json::array({"name", "tipBone"}));
        const nlohmann::json constraint = chain["properties"]["constraints"]["items"];
        CHECK(constraint["additionalProperties"] == false);
        CHECK(constraint["properties"]["type"]["enum"] == nlohmann::json::array({"None", "Hinge", "Cone", "BallAndSocket"}));
        CHECK(registry.find("animation_ik_set_target")->inputSchema["required"] ==
              nlohmann::json::array({"entity", "chain", "position"}));
    }

    TEST_CASE("chainLength walks up from the tip and a new component starts from the mesh's chains")
    {
        IkToolFixture f;

        const nlohmann::json out = f.callOk("animation_ik_set", withChains(heroId, nlohmann::json::array({leftArm()})));

        CHECK(out["changed"] == true);
        CHECK(out["componentAdded"] == true);
        CHECK(out["hasAnimator"] == true);
        CHECK(out["warnings"].empty());
        // AddIKComponent copied the mesh's LeftLeg; ONE SetIKChains wrote the merged list.
        checkLog(f.engine.log, {"AddIKComponent", "SetIKChains", "PushUndo"});
        CHECK(f.chainNames(heroId) == std::vector<std::string>{"LeftLeg", "LeftArm"});

        const IKChainConfig& arm = f.engine.components.at(heroId).back();
        CHECK(arm.tipBoneName == "Hand_L");
        CHECK(arm.chainBoneNames == std::vector<std::string>{"UpperArm_L", "LowerArm_L", "Hand_L"});
        CHECK(arm.weight == 1.0f);
        CHECK(arm.enabled);
        CHECK(arm.constraints.empty());

        REQUIRE(out["chains"].size() == 2);
        CHECK(out["chains"][1]["chainBones"] == nlohmann::json::array({"UpperArm_L", "LowerArm_L", "Hand_L"}));

        SUBCASE("the walk reaches the root")
        {
            nlohmann::json spine{{"name", "Reach"}, {"tipBone", "Hand_R"}, {"chainLength", 6}};
            f.callOk("animation_ik_set", withChains(heroId, nlohmann::json::array({spine})));
            CHECK(f.engine.components.at(heroId).back().chainBoneNames ==
                  std::vector<std::string>{"Hips", "Spine", "Chest", "UpperArm_R", "LowerArm_R", "Hand_R"});
        }
    }

    TEST_CASE("invalid chains fail the whole call before anything changes")
    {
        IkToolFixture f;
        auto set = [&f](nlohmann::json chain)
        {
            return f.argError("animation_ik_set", withChains(heroId, nlohmann::json::array({std::move(chain)})));
        };
        auto constrained = [](nlohmann::json constraints)
        {
            return nlohmann::json{{"name", "Arm"}, {"tipBone", "Hand_R"}, {"chainLength", 3},
                                  {"constraints", std::move(constraints)}};
        };
        const nlohmann::json none{{"type", "None"}};

        // chainLength
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_L"}, {"chainLength", 7}}),
                       "'chains[0].chainLength' is 7, but tip bone 'Hand_L' has only 6 bone(s) up to the root"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_L"}, {"chainLength", 1}}),
                       "'chains[0].chainLength' must be between 2 and 256"));
        // bone names and paths
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand"}, {"chainLength", 2}}),
                       "'chains[0].tipBone' names 'Hand', which is not a bone of 'C:/VFTestProject/characters/Hero.vfMesh'"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_L"}}), "needs exactly one of 'chainBones'"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_L"}, {"chainLength", 2},
                            {"chainBones", nlohmann::json::array({"LowerArm_L", "Hand_L"})}}),
                       "needs exactly one of 'chainBones'"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_R"}, {"chainBones", nlohmann::json::array({"Elbow"})}}),
                       "'chains[0].chainBones[0]' names 'Elbow', which is not a bone of"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_R"},
                            {"chainBones", nlohmann::json::array({"Chest", "LowerArm_R"})}}),
                       "'LowerArm_R' is not a child of 'Chest'"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_R"}, {"chainBones", nlohmann::json::array({"Spine", "Chest"})}}),
                       "must end with the tip bone 'Hand_R' or its parent (it ends with 'Chest')"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_R"}, {"chainBones", nlohmann::json::array({"Hand_R"})}}),
                       "an IK chain needs at least 2 joints"));
        // name, weight, keys
        CHECK(contains(set({{"name", ""}, {"tipBone", "Hand_R"}, {"chainLength", 2}}), "'chains[0].name' must not be empty"));
        CHECK(contains(set({{"tipBone", "Hand_R"}, {"chainLength", 2}}), "'chains[0].name' is required"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_R"}, {"chainLength", 2}, {"weight", 1.5}}),
                       "'chains[0].weight' must be between"));
        CHECK(contains(set({{"name", "Arm"}, {"tipBone", "Hand_R"}, {"chainLength", 2}, {"stiffness", 1}}),
                       "unknown chains[0] key 'stiffness'"));
        // constraints
        CHECK(contains(set(constrained(nlohmann::json::array({nlohmann::json{{"type", "Twist"}}}))),
                       "'chains[0].constraints[0].type' must be one of None, Hinge, Cone, BallAndSocket"));
        CHECK(contains(set(constrained(nlohmann::json::array({nlohmann::json{{"hingeAxis", {1.0, 0.0, 0.0}}}}))),
                       "'chains[0].constraints[0].type' is required"));
        CHECK(contains(set(constrained(nlohmann::json::array(
                           {nlohmann::json{{"type", "Hinge"}, {"hingeAxis", {0.0, 0.0, 0.0}}}}))),
                       "'chains[0].constraints[0].hingeAxis' must not be the zero vector"));
        CHECK(contains(set(constrained(nlohmann::json::array({nlohmann::json{{"type", "Cone"}, {"coneAngle", 200}}}))),
                       "'chains[0].constraints[0].coneAngle' must be between"));
        CHECK(contains(set(constrained(nlohmann::json::array(
                           {nlohmann::json{{"type", "Cone"}, {"hingeAxis", {1.0, 0.0, 0.0}}}}))),
                       "unknown chains[0].constraints[0] (Cone) key 'hingeAxis'"));
        CHECK(contains(set(constrained(nlohmann::json::array(
                           {nlohmann::json{{"type", "BallAndSocket"}, {"twistMin", 10}}}))),
                       "'chains[0].constraints[0].twistMin' must be between"));
        CHECK(contains(set(constrained(nlohmann::json::array({none, none, none, none}))),
                       "'chains[0].constraints' has 4 entries but the chain has 3 joints"));

        // The request as a whole.
        const nlohmann::json arm = leftArm();
        CHECK(contains(f.argError("animation_ik_set", withChains(heroId, nlohmann::json::array({arm, arm}))),
                       "'chains' names the chain 'LeftArm' twice"));
        nlohmann::json both = withChains(heroId, nlohmann::json::array({arm}));
        both["remove"] = nlohmann::json::array({"LeftArm"});
        CHECK(contains(f.argError("animation_ik_set", both), "is both in 'chains' and in 'remove'"));
        CHECK(contains(f.argError("animation_ik_set", {{"entity", heroId}, {"remove", nlohmann::json::array({"Tail"})}}),
                       "there is no IK chain named 'Tail' to remove (chains: LeftLeg)"));
        CHECK(contains(f.argError("animation_ik_set", {{"entity", heroId}, {"replaceAll", true},
                                                       {"remove", nlohmann::json::array({"LeftLeg"})}}),
                       "cannot be combined with replaceAll"));
        CHECK(contains(f.argError("animation_ik_set", {{"entity", heroId}, {"chain", nlohmann::json::array({arm})}}),
                       "unknown argument 'chain' for animation_ik_set"));
        CHECK(contains(f.argError("animation_ik_set", {{"entity", heroId}, {"chains", arm}}),
                       "argument 'chains' must be an array"));

        CHECK(f.engine.log.empty());
        CHECK(f.engine.components.empty());
        CHECK(f.engine.undoStack.empty());
    }

    TEST_CASE("chainBones may stop at the tip's parent and constraint angles are degrees")
    {
        IkToolFixture f;
        const nlohmann::json chain{
            {"name", "RightArm"},
            {"tipBone", "Hand_R"},
            {"chainBones", nlohmann::json::array({"UpperArm_R", "LowerArm_R"})},
            {"weight", 0.5},
            {"enabled", false},
            {"constraints", nlohmann::json::array({
                nlohmann::json{{"type", "BallAndSocket"}, {"swingAngle", 60}, {"twistMin", -30}, {"twistMax", 15}},
                nlohmann::json{{"type", "hinge"}, {"hingeAxis", {0.0, 0.0, 2.0}}},
                nlohmann::json{{"type", "Cone"}, {"coneAngle", 90}}
            })}
        };

        const nlohmann::json out = f.callOk("animation_ik_set", withChains(heroId, nlohmann::json::array({chain})));

        const IKChainConfig& stored = f.engine.components.at(heroId).back();
        CHECK(stored.chainBoneNames == std::vector<std::string>{"UpperArm_R", "LowerArm_R", "Hand_R"});
        CHECK(stored.weight == doctest::Approx(0.5f));
        CHECK_FALSE(stored.enabled);
        REQUIRE(stored.constraints.size() == 3);
        CHECK(stored.constraints[0].type == JointConstraintType::BallAndSocket);
        CHECK(stored.constraints[0].swingAngle == doctest::Approx(glm::radians(60.0f)));
        CHECK(stored.constraints[0].twistMin == doctest::Approx(glm::radians(-30.0f)));
        CHECK(stored.constraints[0].twistMax == doctest::Approx(glm::radians(15.0f)));
        CHECK(stored.constraints[1].type == JointConstraintType::Hinge);
        CHECK(stored.constraints[1].hingeAxis == glm::vec3(0.0f, 0.0f, 1.0f));
        CHECK(stored.constraints[2].type == JointConstraintType::Cone);
        CHECK(stored.constraints[2].coneAngle == doctest::Approx(glm::half_pi<float>()));

        // Reported back in degrees, in the shape animation_ik_set takes.
        const nlohmann::json& reported = out["chains"].back();
        CHECK(reported["chainBones"] == nlohmann::json::array({"UpperArm_R", "LowerArm_R", "Hand_R"}));
        CHECK(reported["weight"] == 0.5);
        CHECK(reported["enabled"] == false);
        CHECK(reported["constraints"][0] ==
              nlohmann::json{{"type", "BallAndSocket"}, {"swingAngle", 60.0}, {"twistMin", -30.0}, {"twistMax", 15.0}});
        CHECK(reported["constraints"][1] == nlohmann::json{{"type", "Hinge"}, {"hingeAxis", {0.0, 0.0, 1.0}}});
        CHECK(reported["constraints"][2] == nlohmann::json{{"type", "Cone"}, {"coneAngle", 90.0}});
    }

    TEST_CASE("upsert keeps a chain's place, remove and replaceAll edit the list, an identical request changes nothing")
    {
        IkToolFixture f;
        f.engine.components[heroId] = {chainConfig("LeftLeg", "Foot_L", {"UpperLeg_L", "LowerLeg_L", "Foot_L"}),
                                       chainConfig("A", "Hand_L", {"LowerArm_L", "Hand_L"}),
                                       chainConfig("B", "Hand_R", {"LowerArm_R", "Hand_R"})};
        const nlohmann::json c{{"name", "C"}, {"tipBone", "Foot_L"}, {"chainLength", 2}};

        SUBCASE("upsert")
        {
            const nlohmann::json a{{"name", "A"}, {"tipBone", "Hand_L"}, {"chainLength", 3}};
            const nlohmann::json out = f.callOk("animation_ik_set", withChains(heroId, nlohmann::json::array({a, c})));
            CHECK(out["componentAdded"] == false);
            checkLog(f.engine.log, {"SetIKChains", "PushUndo"});
            CHECK(f.chainNames(heroId) == std::vector<std::string>{"LeftLeg", "A", "B", "C"});
            CHECK(f.engine.components.at(heroId)[1].chainBoneNames ==
                  std::vector<std::string>{"UpperArm_L", "LowerArm_L", "Hand_L"});
        }

        SUBCASE("remove (a repeated name counts once)")
        {
            f.callOk("animation_ik_set", {{"entity", heroId}, {"remove", nlohmann::json::array({"A", "LeftLeg", "A"})}});
            CHECK(f.chainNames(heroId) == std::vector<std::string>{"B"});
        }

        SUBCASE("replaceAll")
        {
            nlohmann::json args = withChains(heroId, nlohmann::json::array({c}));
            args["replaceAll"] = true;
            f.callOk("animation_ik_set", args);
            CHECK(f.chainNames(heroId) == std::vector<std::string>{"C"});
        }

        SUBCASE("replaceAll without chains empties the list and keeps the component")
        {
            f.callOk("animation_ik_set", {{"entity", heroId}, {"replaceAll", true}});
            REQUIRE(f.engine.components.contains(heroId));
            CHECK(f.engine.components.at(heroId).empty());
        }

        SUBCASE("an identical chain changes nothing and pushes nothing")
        {
            const nlohmann::json b{{"name", "B"}, {"tipBone", "Hand_R"},
                                   {"chainBones", nlohmann::json::array({"LowerArm_R", "Hand_R"})}};
            const nlohmann::json out = f.callOk("animation_ik_set", withChains(heroId, nlohmann::json::array({b})));
            CHECK(out["changed"] == false);
            CHECK(f.engine.log.empty());
            CHECK(f.engine.undoStack.empty());
        }

        SUBCASE("an existing component needs something to change")
        {
            CHECK(contains(f.argError("animation_ik_set", {{"entity", heroId}}), "nothing to change"));
            CHECK(f.engine.log.empty());
        }
    }

    TEST_CASE("chain edits need a skinned mesh")
    {
        IkToolFixture f;
        const nlohmann::json chains = nlohmann::json::array({leftArm()});

        CHECK(contains(f.runtimeError("animation_ik_set", withChains(emptyId, chains)), "has no mesh"));
        CHECK(contains(f.runtimeError("animation_ik_set", withChains(crateId, chains)), "has no skeleton"));
        f.engine.skeletons.erase(heroMesh);
        CHECK(contains(f.runtimeError("animation_ik_set", withChains(heroId, chains)), "Cannot read the skeleton"));
        CHECK(contains(f.runtimeError("animation_ik_set", withChains(999, chains)), "does not exist"));
        CHECK(f.engine.log.empty());
    }

    TEST_CASE("IkStateUndo replays the whole IK state through Add/Remove + SetIKChains and follows re-minted ids")
    {
        IkToolFixture f;

        f.callOk("animation_ik_set", withChains(heroId, nlohmann::json::array({leftArm()})));
        REQUIRE(f.engine.undoStack.size() == 1);
        const std::shared_ptr<services::IUndoableCommand> created = f.engine.undoStack.back();
        CHECK(created->getDescription() == "MCP: Set IK chains (Hero)");
        CHECK(created->getMemoryFootprint() > 0);

        f.engine.log.clear();
        created->undo(); // there was no component before
        checkLog(f.engine.log, {"RemoveIKComponent"});
        CHECK_FALSE(f.engine.components.contains(heroId));

        f.engine.log.clear();
        created->execute(); // AddIKComponent copies the mesh's chains; SetIKChains makes them exact
        checkLog(f.engine.log, {"AddIKComponent", "SetIKChains"});
        CHECK(f.chainNames(heroId) == std::vector<std::string>{"LeftLeg", "LeftArm"});

        // An edit of an existing component: undo restores its chains and keeps it.
        f.callOk("animation_ik_set", {{"entity", heroId}, {"remove", nlohmann::json::array({"LeftLeg"})}});
        REQUIRE(f.engine.undoStack.size() == 2);
        const std::shared_ptr<services::IUndoableCommand> removed = f.engine.undoStack.back();
        f.engine.log.clear();
        removed->undo();
        checkLog(f.engine.log, {"SetIKChains"});
        CHECK(f.chainNames(heroId) == std::vector<std::string>{"LeftLeg", "LeftArm"});
        removed->execute();
        CHECK(f.chainNames(heroId) == std::vector<std::string>{"LeftArm"});

        // The entity came back under a new id (an undone delete): both entries follow it.
        f.engine.moveEntity(heroId, 42);
        mcp::undo::EntityIdRemap::instance().record(static_cast<uint32_t>(heroId), 42);
        removed->undo();
        CHECK(f.chainNames(42) == std::vector<std::string>{"LeftLeg", "LeftArm"});
        created->undo();
        CHECK_FALSE(f.engine.components.contains(42));

        // The entity is gone: the entry throws, the pushed batch logs it and is consumed.
        f.engine.entities.erase(42);
        mcp::undo::IkStateUndo direct(static_cast<uint32_t>(heroId), "Hero", mcp::undo::IkState{},
                                      mcp::undo::IkState{true, {}});
        CHECK_THROWS_AS(direct.execute(), std::runtime_error);
        CHECK_NOTHROW(created->execute());
        CHECK_FALSE(f.engine.components.contains(42));
    }

    TEST_CASE("animation_ik_remove needs a component and is undone exactly")
    {
        IkToolFixture f;
        CHECK(contains(f.runtimeError("animation_ik_remove", {{"entity", heroId}}), "has no IK component"));

        IKChainConfig arm = chainConfig("LeftArm", "Hand_L", {"UpperArm_L", "LowerArm_L", "Hand_L"});
        arm.weight = 0.25f;
        f.engine.components[heroId] = {arm};

        const nlohmann::json out = f.callOk("animation_ik_remove", {{"entity", heroId}});
        REQUIRE(out["removedChains"].size() == 1);
        CHECK(out["removedChains"][0]["name"] == "LeftArm");
        CHECK_FALSE(f.engine.components.contains(heroId));
        REQUIRE(f.engine.undoStack.size() == 1);
        const auto entry = f.engine.undoStack.back();
        CHECK(entry->getDescription() == "MCP: Remove IK component (Hero)");

        f.engine.log.clear();
        entry->undo();
        checkLog(f.engine.log, {"AddIKComponent", "SetIKChains"});
        REQUIRE(f.engine.components.contains(heroId));
        REQUIRE(f.engine.components.at(heroId).size() == 1); // not the mesh's LeftLeg: the exact old list
        CHECK(f.engine.components.at(heroId)[0].chainName == "LeftArm");
        CHECK(f.engine.components.at(heroId)[0].weight == doctest::Approx(0.25f));

        entry->execute();
        CHECK_FALSE(f.engine.components.contains(heroId));
    }

    TEST_CASE("chain edits are refused in Play mode and during a scene load; reads and targets are not")
    {
        IkToolFixture f;
        f.engine.components[heroId] = {chainConfig("LeftArm", "Hand_L", {"UpperArm_L", "LowerArm_L", "Hand_L"})};
        const nlohmann::json chains = nlohmann::json::array({leftArm()});

        f.engine.playMode = true;
        const mcp::ToolResult set = f.call("animation_ik_set", withChains(heroId, chains));
        CHECK(set.isError);
        CHECK(contains(set.text, "Play mode"));
        const mcp::ToolResult remove = f.call("animation_ik_remove", {{"entity", heroId}});
        CHECK(remove.isError);
        CHECK(contains(remove.text, "Play mode"));
        CHECK(f.engine.log.empty());

        CHECK_FALSE(f.call("animation_ik_get", {{"entity", heroId}}).isError);
        CHECK_FALSE(f.call("animation_ik_set_target", {{"entity", heroId}, {"chain", "LeftArm"},
                                                       {"position", {1.0, 2.0, 3.0}}}).isError);

        f.engine.playMode = false;
        f.engine.loadPending = true;
        const mcp::ToolResult loading = f.call("animation_ik_set", withChains(heroId, chains));
        CHECK(loading.isError);
        CHECK(contains(loading.text, "scene load"));
        CHECK(f.engine.undoStack.empty());
    }

    TEST_CASE("animation_ik_set_target sets the weight before the target and refuses unknown chains")
    {
        IkToolFixture f;
        f.engine.components[heroId] = {chainConfig("LeftArm", "Hand_L", {"UpperArm_L", "LowerArm_L", "Hand_L"})};

        const nlohmann::json out = f.callOk("animation_ik_set_target", {
            {"entity", heroId},
            {"chain", "LeftArm"},
            {"position", {1.0, 2.0, 3.0}},
            {"rotation", {0.0, 90.0, 0.0}},
            {"weight", 0.5}
        });

        checkLog(f.engine.log, {"SetIKChainWeight:LeftArm", "SetIKTarget:LeftArm"});
        CHECK(f.engine.components.at(heroId)[0].weight == doctest::Approx(0.5f));
        const FakeIkEngine::Target& target = f.engine.targets.at({heroId, "LeftArm"});
        CHECK(target.position == glm::vec3(1.0f, 2.0f, 3.0f));
        REQUIRE(target.rotation.has_value());
        const glm::quat expected = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK(std::abs(glm::dot(*target.rotation, expected)) == doctest::Approx(1.0f));
        CHECK(out["weight"] == 0.5);
        CHECK(out["enabled"] == true);
        CHECK(out["warnings"].empty());
        CHECK(f.engine.undoStack.empty()); // runtime only

        SUBCASE("without a weight only the target is set")
        {
            f.engine.log.clear();
            f.callOk("animation_ik_set_target", {{"entity", heroId}, {"chain", "LeftArm"}, {"position", {0.0, 1.0, 0.0}}});
            checkLog(f.engine.log, {"SetIKTarget:LeftArm"});
            CHECK_FALSE(f.engine.targets.at({heroId, "LeftArm"}).rotation.has_value());
        }

        SUBCASE("an unknown chain, a missing component or a bad weight dispatch nothing")
        {
            f.engine.log.clear();
            CHECK(contains(f.runtimeError("animation_ik_set_target",
                                          {{"entity", heroId}, {"chain", "Tail"}, {"position", {0.0, 0.0, 0.0}}}),
                           "has no IK chain 'Tail' (chains: LeftArm)"));
            CHECK(contains(f.runtimeError("animation_ik_set_target",
                                          {{"entity", mannequinId}, {"chain", "LeftArm"}, {"position", {0.0, 0.0, 0.0}}}),
                           "has no IK component"));
            CHECK(contains(f.argError("animation_ik_set_target", {{"entity", heroId}, {"chain", "LeftArm"},
                                                                  {"position", {0.0, 0.0, 0.0}}, {"weight", 2.0}}),
                           "argument 'weight' must be between"));
            CHECK(contains(f.argError("animation_ik_set_target", {{"entity", heroId}, {"chain", "LeftArm"}}),
                           "missing required argument 'position'"));
            CHECK(f.engine.log.empty());
        }

        SUBCASE("an entity without an Animator is warned about")
        {
            f.engine.components[mannequinId] = f.engine.components.at(heroId);
            const nlohmann::json warned = f.callOk("animation_ik_set_target", {{"entity", mannequinId}, {"chain", "LeftArm"},
                                                                               {"position", {0.0, 0.0, 0.0}}});
            CHECK(warned["hasAnimator"] == false);
            CHECK(anyContains(warned["warnings"], "has no Animator"));
        }
    }

    TEST_CASE("animation_ik_get reports chains in degrees, the mesh's chains, the bones and skeleton problems")
    {
        IkToolFixture f;
        IKChainConfig arm = chainConfig("LeftArm", "Hand_L", {"UpperArm_L", "LowerArm_L", "Hand_L"});
        JointConstraint cone;
        cone.type = JointConstraintType::Cone;
        cone.coneAngle = glm::radians(30.0f);
        arm.constraints = {JointConstraint{}, cone};
        f.engine.components[mannequinId] = {arm, chainConfig("Tail", "Tail_02", {"Tail_01", "Tail_02"})};

        const nlohmann::json out = f.callOk("animation_ik_get", {{"entity", mannequinId}, {"includeBones", true}});

        CHECK(out["hasComponent"] == true);
        CHECK(out["hasAnimator"] == false);
        CHECK(out["skinned"] == true);
        CHECK(out["boneCount"] == 12);
        REQUIRE(out["mesh"].is_string());
        CHECK(contains(out["mesh"].get<std::string>(), "characters/Hero.vfMesh"));
        REQUIRE(out["chains"].size() == 2);
        CHECK(out["chains"][0]["constraints"][0] == nlohmann::json{{"type", "None"}});
        CHECK(out["chains"][0]["constraints"][1] == nlohmann::json{{"type", "Cone"}, {"coneAngle", 30.0}});
        REQUIRE(out["meshChains"].size() == 1);
        CHECK(out["meshChains"][0]["name"] == "LeftLeg");
        REQUIRE(out["bones"].size() == 12);
        CHECK(out["bones"][0] == nlohmann::json{{"name", "Hips"}, {"parent", nullptr}});
        CHECK(out["bones"][5] == nlohmann::json{{"name", "Hand_L"}, {"parent", "LowerArm_L"}});

        const nlohmann::json& warnings = out["warnings"];
        CHECK(anyContains(warnings, "has no Animator"));
        CHECK(anyContains(warnings, "IK chain 'Tail': bone 'Tail_01' is not in the skeleton"));
        CHECK(anyContains(warnings, "IK chain 'Tail': tip bone 'Tail_02' is not in the skeleton"));
        CHECK_FALSE(anyContains(warnings, "IK chain 'LeftArm'"));

        SUBCASE("an entity without a component or a mesh")
        {
            const nlohmann::json bare = f.callOk("animation_ik_get", {{"entity", emptyId}});
            CHECK(bare["hasComponent"] == false);
            CHECK(bare["chains"].empty());
            CHECK(bare["mesh"].is_null());
            CHECK(bare["skinned"] == false);
            CHECK_FALSE(bare.contains("bones"));
            CHECK(anyContains(bare["warnings"], "has no mesh"));
        }

        CHECK(contains(f.argError("animation_ik_get", {{"entity", mannequinId}, {"bones", true}}),
                       "unknown argument 'bones' for animation_ik_get"));
    }
}
