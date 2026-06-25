#include <doctest.h>

// VK-1433 Phase 4a — buildPrefabRigDescFromEntity (PrefabRigLiveDescBuilder.hpp) coverage.
//
// The window's Phase-4 spine re-derives the rig description from a live, isolated sandbox subtree
// instead of parsing the prefab JSON. This TU pins three things CPU-only (no GPU, no .vfMesh load):
//
//   1. PARITY with the JSON builder (buildPrefabRigDescDTO). A synthetic sandbox subtree and an
//      equivalent PrefabEntityNode tree must produce field-equal DTOs (parts count, paths order,
//      parentPartIndex, attachParentSocket, attachChildRotation/Scale) — so the two builders agree
//      and the DFS-pre-order k-th-mesh-bearing part mapping is identical.
//   2. partEntities[k] maps to the source entity of part k.
//   3. Authored hide: inactive non-root entities prune their descendants and compact part indices,
//      while an inactive sandbox root still yields its active children because that flag is only
//      preview isolation.
//
// The real scene/material/socket CQRS handlers (HierarchyService, MaterialService, SocketService,
// ...) are not constructed in the CPU-only test runner, so this TU registers minimal fakes that
// read a per-test entity store. AssetDatabase resolves synthetic asset paths so AssetRef::resolve()
// returns the same path strings the JSON builder consumes directly.

#include "windows/preview/PrefabRigLiveDescBuilder.hpp" // buildPrefabRigDescFromEntity (under test)
#include "windows/preview/PrefabRigDescBuilder.hpp"     // buildPrefabRigDescDTO + PrefabEntityNode
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"               // VK-1433 Phase 4c: real sandbox subtree for the save roundtrip
#include "scene/SceneGraphSystem.hpp"     // LoadPrefab target
#include "serialization/PrefabSerialization.hpp" // real savePrefab/loadPrefab (== Save/LoadPrefabCommand body)
#include "data/EntityConversion.hpp"
#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"
#include "resource/AssetTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/physics/IKEvents.hpp"
#include "animator/IKTypes.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    // ---- Per-test entity store, read by the fake CQRS handlers ------------------------------
    struct FakeMesh
    {
        std::string meshPath;
        std::string animatorPath;
        std::string retargetPath;
    };
    struct FakeSocket
    {
        std::string parentEntityName;
        std::string socketName;
    };
    struct FakeNode
    {
        std::string name;
        services::TransformData localTransform;
        std::vector<services::EntityHandle> children;
        bool isActive = true;
        std::optional<FakeMesh> mesh;
        std::optional<std::string> defaultMaterialPath; // resolved by AssetDatabase below
        std::map<std::string, std::string> subMeshMaterials; // submesh name -> material path
        std::optional<FakeSocket> socket;
        std::vector<animator::ik::IKChainConfig> ikChains; // IKTargetComponent chains
    };

    struct FakeStore
    {
        std::unordered_map<uint64_t, FakeNode> nodes; // entity id -> node
        std::optional<services::EntityHandle> selected;
        // VK-1433 Phase 4b — id allocator for entities created via the fake CreateEntityCommand. Starts
        // high so a synthetic-created id never collides with a makeNode() entt id.
        uint64_t nextId = 1'000'000;

        void clear() { nodes.clear(); selected.reset(); nextId = 1'000'000; }
    };

    FakeStore& store()
    {
        static FakeStore s;
        return s;
    }

    // Register synthetic asset paths once so AssetRef::resolve() yields stable path strings, and
    // register the fake CQRS query handlers once. Idempotent (the dispatcher would otherwise warn
    // about replacing a handler).
    asset::AssetRef refFor(const std::string& path, resource::AssetType type)
    {
        if (path.empty()) return asset::AssetRef::invalid();
        auto existing = asset::AssetDatabase::instance().getGUID(path);
        if (existing.has_value()) return asset::AssetRef::fromGUID(*existing);
        return asset::AssetRef::fromGUID(asset::AssetDatabase::instance().registerAsset(path, type));
    }

    void ensureFakeHandlers()
    {
        static bool registered = false;
        if (registered) return;
        registered = true;
        auto& d = events::EventDispatcher::instance();

        // VK-1433 Phase 4c (P4bcdTest): the five rig queries below read the FAKE store first, and
        // fall back to the REAL EntityRegistry when the id is NOT in the store. Every makeNode()/
        // fake-CreateEntity id is always in the store, so the existing store-driven cases hit the
        // store branch unchanged; the registry fallback fires only for entities that exist purely
        // in the registry — the entities a REAL PrefabSerialization save->reload re-instantiates
        // (fresh entt ids, never registered in the store). This lets the 4c real-roundtrip suite
        // re-derive the rig from the reloaded sandbox via buildPrefabRigDescFromEntity, exactly as
        // the window does after LoadPrefab.
        d.registerQueryHandler<events::scene::GetEntityQuery>(
            [](const events::scene::GetEntityQuery& q) -> std::optional<services::EntityData>
            {
                auto it = store().nodes.find(q.entity.id);
                if (it != store().nodes.end())
                {
                    services::EntityData data;
                    data.handle = q.entity;
                    data.name = it->second.name;
                    data.isActive = it->second.isActive;
                    data.localTransform = it->second.localTransform;
                    data.children = it->second.children;
                    return data;
                }
                // Registry fallback (reloaded real entities).
                auto& registry = scene::EntityRegistry::getRegistry();
                const entt::entity e = services::internal::fromHandle(q.entity);
                if (!registry.valid(e)) return std::nullopt;
                services::EntityData data;
                data.handle = q.entity;
                if (const auto* name = registry.try_get<components::NameComponent>(e))
                {
                    data.name = name->name;
                    data.isActive = name->isActive;
                }
                if (const auto* xf = registry.try_get<components::TransformComponent>(e))
                {
                    data.localTransform.position = xf->position;
                    data.localTransform.rotation = xf->rotation;
                    data.localTransform.scale = xf->scale;
                }
                if (const auto* kids = registry.try_get<components::ChildrenComponent>(e))
                    for (entt::entity child : kids->children)
                        if (registry.valid(child))
                            data.children.push_back(services::internal::toHandle(child));
                return data;
            });

        d.registerQueryHandler<events::scene::GetMeshDataQuery>(
            [](const events::scene::GetMeshDataQuery& q) -> std::optional<services::MeshData>
            {
                auto it = store().nodes.find(q.entity.id);
                if (it != store().nodes.end())
                {
                    if (!it->second.mesh.has_value()) return std::nullopt;
                    const FakeMesh& m = *it->second.mesh;
                    services::MeshData data;
                    data.meshRef = refFor(m.meshPath, resource::AssetType::Mesh);
                    data.animatorRef = refFor(m.animatorPath, resource::AssetType::Animator);
                    data.retargetRef = refFor(m.retargetPath, resource::AssetType::RetargetMap);
                    return data;
                }
                // Registry fallback: read the MeshComponent refs verbatim (the live builder resolves
                // them, so GUID->path resolution matches the pre-save derivation).
                auto& registry = scene::EntityRegistry::getRegistry();
                const entt::entity e = services::internal::fromHandle(q.entity);
                const auto* mesh = registry.valid(e)
                    ? registry.try_get<components::MeshComponent>(e) : nullptr;
                if (!mesh) return std::nullopt;
                services::MeshData data;
                data.meshRef = mesh->meshRef;
                data.animatorRef = mesh->animatorRef;
                data.retargetRef = mesh->retargetRef;
                return data;
            });

        d.registerQueryHandler<events::material::GetMaterialDataQuery>(
            [](const events::material::GetMaterialDataQuery& q) -> std::optional<services::MaterialData>
            {
                auto it = store().nodes.find(q.entity.id);
                if (it != store().nodes.end())
                {
                    const FakeNode& n = it->second;
                    if (!n.defaultMaterialPath.has_value() && n.subMeshMaterials.empty())
                        return std::nullopt;
                    services::MaterialData data;
                    if (n.defaultMaterialPath.has_value())
                        data.defaultMaterialRef = refFor(*n.defaultMaterialPath, resource::AssetType::Material);
                    for (const auto& [submeshName, path] : n.subMeshMaterials)
                        data.subMeshMaterials[submeshName] = refFor(path, resource::AssetType::Material);
                    return data;
                }
                // Registry fallback: read the MaterialComponent refs verbatim.
                auto& registry = scene::EntityRegistry::getRegistry();
                const entt::entity e = services::internal::fromHandle(q.entity);
                const auto* mat = registry.valid(e)
                    ? registry.try_get<components::MaterialComponent>(e) : nullptr;
                if (!mat) return std::nullopt;
                if (!mat->defaultMaterialRef.isValid() && mat->subMeshMaterials.empty())
                    return std::nullopt;
                services::MaterialData data;
                data.defaultMaterialRef = mat->defaultMaterialRef;
                for (const auto& [submeshName, ref] : mat->subMeshMaterials)
                    data.subMeshMaterials[submeshName] = ref;
                return data;
            });

        d.registerQueryHandler<events::socket::GetSocketAttachmentDataQuery>(
            [](const events::socket::GetSocketAttachmentDataQuery& q)
                -> std::optional<events::socket::SocketAttachmentData>
            {
                auto it = store().nodes.find(q.entity.id);
                if (it != store().nodes.end())
                {
                    if (!it->second.socket.has_value()) return std::nullopt;
                    events::socket::SocketAttachmentData data;
                    data.parentEntityName = it->second.socket->parentEntityName;
                    data.socketName = it->second.socket->socketName;
                    return data;
                }
                // Registry fallback.
                auto& registry = scene::EntityRegistry::getRegistry();
                const entt::entity e = services::internal::fromHandle(q.entity);
                const auto* socket = registry.valid(e)
                    ? registry.try_get<components::SocketAttachmentComponent>(e) : nullptr;
                if (!socket) return std::nullopt;
                events::socket::SocketAttachmentData data;
                data.parentEntityName = socket->parentEntityName;
                data.socketName = socket->socketName;
                return data;
            });

        d.registerQueryHandler<events::ik::GetIKChainConfigsQuery>(
            [](const events::ik::GetIKChainConfigsQuery& q)
                -> std::vector<animator::ik::IKChainConfig>
            {
                auto it = store().nodes.find(q.entity.id);
                if (it != store().nodes.end()) return it->second.ikChains;
                // Registry fallback.
                auto& registry = scene::EntityRegistry::getRegistry();
                const entt::entity e = services::internal::fromHandle(q.entity);
                const auto* ik = registry.valid(e)
                    ? registry.try_get<components::IKTargetComponent>(e) : nullptr;
                if (!ik) return {};
                return ik->chains;
            });

        // ---- VK-1433 Phase 4b/4c: fake MUTATION command handlers ---------------------------------
        // These mutate the same FakeStore the queries read, so a test can drive the exact CQRS the
        // window issues (create / delete / reparent / reorder / rename / set-transform / set-mesh /
        // set-material) and then re-derive the rig to assert the edit landed.

        // Helper: detach `child` from whatever parent currently lists it (so reparent/reorder/delete
        // don't leave a dangling child entry).
        auto detachFromParent = [](services::EntityHandle child)
        {
            for (auto& [id, node] : store().nodes)
            {
                auto& kids = node.children;
                kids.erase(std::remove(kids.begin(), kids.end(), child), kids.end());
            }
        };

        d.registerCommandHandler<events::scene::CreateEntityCommand>(
            [](const events::scene::CreateEntityCommand& c) -> services::EntityHandle
            {
                const services::EntityHandle h{store().nextId++};
                FakeNode node;
                node.name = c.name;
                store().nodes[h.id] = node;
                if (c.parent.has_value() && c.parent->isValid())
                    store().nodes[c.parent->id].children.push_back(h);
                return h;
            });

        d.registerCommandHandler<events::scene::DeleteEntityCommand>(
            [detachFromParent](const events::scene::DeleteEntityCommand& c) -> bool
            {
                if (!store().nodes.count(c.entity.id)) return false;
                // Recursively erase the subtree.
                std::function<void(services::EntityHandle)> eraseSub = [&](services::EntityHandle e)
                {
                    auto it = store().nodes.find(e.id);
                    if (it == store().nodes.end()) return;
                    std::vector<services::EntityHandle> kids = it->second.children;
                    for (auto k : kids) eraseSub(k);
                    store().nodes.erase(e.id);
                };
                detachFromParent(c.entity);
                eraseSub(c.entity);
                return true;
            });

        d.registerCommandHandler<events::scene::ReparentEntityCommand>(
            [detachFromParent](const events::scene::ReparentEntityCommand& c) -> bool
            {
                if (!store().nodes.count(c.entity.id) || !store().nodes.count(c.newParent.id)) return false;
                detachFromParent(c.entity);
                store().nodes[c.newParent.id].children.push_back(c.entity);
                return true;
            });

        d.registerCommandHandler<events::scene::ReorderEntityCommand>(
            [detachFromParent](const events::scene::ReorderEntityCommand& c) -> bool
            {
                if (!store().nodes.count(c.entity.id) || !store().nodes.count(c.newParent.id)) return false;
                detachFromParent(c.entity);
                auto& kids = store().nodes[c.newParent.id].children;
                int idx = c.insertIndex;
                if (idx < 0 || idx > static_cast<int>(kids.size())) idx = static_cast<int>(kids.size());
                kids.insert(kids.begin() + idx, c.entity);
                return true;
            });

        d.registerCommandHandler<events::scene::SetEntityNameCommand>(
            [](const events::scene::SetEntityNameCommand& c)
            {
                auto it = store().nodes.find(c.entity.id);
                if (it != store().nodes.end()) it->second.name = c.newName;
            });

        d.registerCommandHandler<events::scene::SetEntityActiveCommand>(
            [](const events::scene::SetEntityActiveCommand& c)
            {
                auto it = store().nodes.find(c.entity.id);
                if (it != store().nodes.end()) it->second.isActive = c.isActive;

                auto& registry = scene::EntityRegistry::getRegistry();
                const entt::entity e = services::internal::fromHandle(c.entity);
                if (registry.valid(e) && registry.all_of<components::NameComponent>(e))
                    registry.get<components::NameComponent>(e).isActive = c.isActive;
            });

        d.registerCommandHandler<events::scene::SetTransformCommand>(
            [](const events::scene::SetTransformCommand& c)
            {
                auto it = store().nodes.find(c.entity.id);
                if (it != store().nodes.end()) it->second.localTransform = c.transform;
            });

        d.registerCommandHandler<events::scene::SetMeshDataCommand>(
            [](const events::scene::SetMeshDataCommand& c) -> bool
            {
                auto it = store().nodes.find(c.entity.id);
                if (it == store().nodes.end()) return false;
                FakeMesh m;
                m.meshPath = c.meshData.meshRef.isValid() ? c.meshData.meshRef.resolve() : std::string();
                m.animatorPath = c.meshData.animatorRef.isValid() ? c.meshData.animatorRef.resolve() : std::string();
                m.retargetPath = c.meshData.retargetRef.isValid() ? c.meshData.retargetRef.resolve() : std::string();
                it->second.mesh = m;
                return true;
            });

        d.registerCommandHandler<events::material::SetDefaultMaterialCommand>(
            [](const events::material::SetDefaultMaterialCommand& c) -> bool
            {
                auto it = store().nodes.find(c.entity.id);
                if (it == store().nodes.end()) return false;
                it->second.defaultMaterialPath = c.materialPath;
                return true;
            });
    }

    // Build a synthetic sandbox node in both the real EntityRegistry (so isActive / marking can be
    // exercised) and the fake store (so the CQRS fakes can serve it). Returns the entity handle.
    services::EntityHandle makeNode(const std::string& name, const services::TransformData& xf)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity e = registry.create();
        auto& nameComp = registry.emplace<components::NameComponent>(e);
        nameComp.name = name;
        nameComp.isActive = true;

        services::EntityHandle h = services::internal::toHandle(e);
        FakeNode node;
        node.name = name;
        node.localTransform = xf;
        store().nodes[h.id] = node;
        return h;
    }

    void setActive(services::EntityHandle h, bool active)
    {
        auto it = store().nodes.find(h.id);
        if (it != store().nodes.end()) it->second.isActive = active;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity e = services::internal::fromHandle(h);
        if (registry.valid(e) && registry.all_of<components::NameComponent>(e))
            registry.get<components::NameComponent>(e).isActive = active;
    }

    void setParentChild(services::EntityHandle parent, services::EntityHandle child)
    {
        store().nodes[parent.id].children.push_back(child);
    }

    void setMesh(services::EntityHandle e, const std::string& mesh, const std::string& animator = {},
                 const std::string& retarget = {})
    {
        FakeMesh m;
        m.meshPath = mesh;
        m.animatorPath = animator;
        m.retargetPath = retarget;
        store().nodes[e.id].mesh = m;
    }

    void setMaterial(services::EntityHandle e, const std::string& mat)
    {
        store().nodes[e.id].defaultMaterialPath = mat;
    }

    void setSubMeshMaterial(services::EntityHandle e, const std::string& submesh, const std::string& mat)
    {
        store().nodes[e.id].subMeshMaterials[submesh] = mat;
    }

    void setSocket(services::EntityHandle e, const std::string& parentName, const std::string& socketName)
    {
        FakeSocket s;
        s.parentEntityName = parentName;
        s.socketName = socketName;
        store().nodes[e.id].socket = s;
    }

    void setIK(services::EntityHandle e, const animator::ik::IKChainConfig& chain)
    {
        store().nodes[e.id].ikChains.push_back(chain);
    }

    animator::ik::IKChainConfig makeChain(const std::string& name, const std::string& tip,
                                          std::vector<std::string> bones, float weight = 1.0f,
                                          bool enabled = true)
    {
        animator::ik::IKChainConfig c;
        c.chainName = name;
        c.tipBoneName = tip;
        c.chainBoneNames = std::move(bones);
        c.weight = weight;
        c.enabled = enabled;
        return c;
    }

    services::TransformData makeXf(glm::vec3 pos, glm::vec3 rot = glm::vec3(0.0f),
                                   glm::vec3 scale = glm::vec3(1.0f))
    {
        services::TransformData t;
        t.position = pos;
        t.rotation = rot;
        t.scale = scale;
        return t;
    }

    // Equivalent PrefabEntityNode (for the JSON parity builder). Mirrors a FakeNode's rig-relevant
    // fields; the test constructs both trees from the same data so the two DTOs can be compared.
    windows::PrefabEntityNode jsonNode(const std::string& name, const services::TransformData& xf)
    {
        windows::PrefabEntityNode n;
        n.name = name;
        n.position = xf.position;
        n.rotation = xf.rotation;
        n.scale = xf.scale;
        return n;
    }

    bool dtoPartsFieldEqual(const services::PrefabRigPartDTO& a, const services::PrefabRigPartDTO& b)
    {
        const float eps = 1e-4f;
        auto vecEq = [&](const glm::vec3& x, const glm::vec3& y)
        {
            return glm::all(glm::lessThan(glm::abs(x - y), glm::vec3(eps)));
        };
        return a.meshPath == b.meshPath &&
               a.animatorPath == b.animatorPath &&
               a.retargetPath == b.retargetPath &&
               a.defaultMaterialPath == b.defaultMaterialPath &&
               a.parentPartIndex == b.parentPartIndex &&
               a.attachParentSocket == b.attachParentSocket &&
               vecEq(a.attachChildRotation, b.attachChildRotation) &&
               vecEq(a.attachChildScale, b.attachChildScale);
    }

    // Per-element matrix equality. The field-equal helper above intentionally omits localTransform
    // (the accumulated world); deeper trees need it asserted explicitly to prove the two builders
    // accumulate ancestor transforms identically.
    bool matEq(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f)
    {
        for (int c = 0; c < 4; ++c)
            if (!glm::all(glm::lessThan(glm::abs(a[c] - b[c]), glm::vec4(eps))))
                return false;
        return true;
    }
}

TEST_SUITE("PrefabRigLiveDescBuilder")
{

// A weapon-on-character rig: root (skeletal mesh) -> hand-bone child? No — the canonical case is a
// skeletal body part as root and a static weapon part socket-attached to it, plus a non-mesh
// grouping node in between to prove the DFS k-th-mesh mapping skips it.
TEST_CASE("live builder matches the JSON builder field-for-field (parity)")
{
    store().clear();
    ensureFakeHandlers();

    // --- Live sandbox subtree ---
    // root "Character" (skeletal) -> "Attachments" (no mesh) -> "Sword" (static, socketed to Character)
    services::EntityHandle root = makeNode("Character", makeXf({0, 0, 0}, {0, 90, 0}, {1, 1, 1}));
    setMesh(root, "assets/character.vfMesh", "assets/character.vfAnimator");
    setMaterial(root, "assets/skin.vfMaterial");

    services::EntityHandle group = makeNode("Attachments", makeXf({0, 1, 0}));
    services::EntityHandle sword = makeNode("Sword", makeXf({0, 0, 0}, {0, 0, 45}, {2, 2, 2}));
    setMesh(sword, "assets/sword.vfMesh"); // static (no animator)
    setSocket(sword, "Character", "RightHandGrip");

    setParentChild(root, group);
    setParentChild(group, sword);

    windows::LiveRigBuildResult live = windows::buildPrefabRigDescFromEntity(root);

    // --- Equivalent PrefabEntityNode tree for the JSON parity builder ---
    windows::PrefabEntityNode jRoot = jsonNode("Character", makeXf({0, 0, 0}, {0, 90, 0}, {1, 1, 1}));
    jRoot.meshPath = "assets/character.vfMesh";
    jRoot.animatorPath = "assets/character.vfAnimator";
    jRoot.defaultMaterialPath = "assets/skin.vfMaterial";

    windows::PrefabEntityNode jGroup = jsonNode("Attachments", makeXf({0, 1, 0}));

    windows::PrefabEntityNode jSword = jsonNode("Sword", makeXf({0, 0, 0}, {0, 0, 45}, {2, 2, 2}));
    jSword.meshPath = "assets/sword.vfMesh";
    jSword.hasSocketAttachment = true;
    jSword.attachParentEntityName = "Character";
    jSword.attachSocketName = "RightHandGrip";

    jGroup.children.push_back(jSword);
    jRoot.children.push_back(jGroup);

    services::PrefabRigDescDTO json = windows::buildPrefabRigDescDTO(jRoot);

    // Same number of parts (2: Character, Sword — the non-mesh "Attachments" is skipped).
    REQUIRE(live.desc.parts.size() == json.parts.size());
    REQUIRE(live.desc.parts.size() == 2);

    for (size_t i = 0; i < live.desc.parts.size(); ++i)
    {
        CAPTURE(i);
        CHECK(dtoPartsFieldEqual(live.desc.parts[i], json.parts[i]));
    }

    // Part 0 is the skeletal root; part 1 is the socketed static sword (DFS pre-order, mesh-only).
    CHECK(live.desc.parts[0].meshPath == "assets/character.vfMesh");
    CHECK(live.desc.parts[0].parentPartIndex == -1);
    CHECK(live.desc.parts[1].meshPath == "assets/sword.vfMesh");
    CHECK(live.desc.parts[1].parentPartIndex == 0);
    CHECK(live.desc.parts[1].attachParentSocket == "RightHandGrip");
    // attachChildRotation/Scale come from the sword's own local transform.
    CHECK(live.desc.parts[1].attachChildRotation.z == doctest::Approx(45.0f));
    CHECK(live.desc.parts[1].attachChildScale.x == doctest::Approx(2.0f));

    // partEntities[k] maps to the source entity of part k.
    REQUIRE(live.partEntities.size() == 2);
    CHECK(live.partEntities[0] == root);
    CHECK(live.partEntities[1] == sword);

    // This rig declares no IK chains, so desc.ik is empty (the IK case below pins the populated path).
    CHECK(live.desc.ik.empty());
}

// VK-1433 Phase 4 (review fix #1) — IK must NOT be dead in the preview. The live builder pulls
// IKTargetComponent chains via GetIKChainConfigsQuery and reconstructs desc.ik exactly like the old
// JSON spine (buildPrefabRigDescDTO), so the assembly (which builds IK ONLY from desc.ik) gets them.
TEST_CASE("live builder reconstructs desc.ik from IKTargetComponent chains (parity, regression guard)")
{
    store().clear();
    ensureFakeHandlers();

    // Skeletal body (root, owns a LeftArm IK chain) + a static sword (the default IK target part).
    services::EntityHandle body = makeNode("Body", makeXf({0, 0, 0}));
    setMesh(body, "assets/body.vfMesh", "assets/body.vfAnimator");
    animator::ik::IKChainConfig arm = makeChain("LeftArm", "Hand", {"Shoulder", "Elbow"}, 0.8f, true);
    setIK(body, arm);

    services::EntityHandle sword = makeNode("Sword", makeXf({0, 0, 0}));
    setMesh(sword, "assets/sword.vfMesh"); // static -> the default target part
    setSocket(sword, "Body", "RightHandGrip");

    setParentChild(body, sword);

    windows::LiveRigBuildResult live = windows::buildPrefabRigDescFromEntity(body);

    // Equivalent JSON tree (PrefabEntityNode.ikChains is what the old spine fed buildPrefabRigDescDTO).
    windows::PrefabEntityNode jBody = jsonNode("Body", makeXf({0, 0, 0}));
    jBody.meshPath = "assets/body.vfMesh";
    jBody.animatorPath = "assets/body.vfAnimator";
    jBody.ikChains.push_back(arm);
    windows::PrefabEntityNode jSword = jsonNode("Sword", makeXf({0, 0, 0}));
    jSword.meshPath = "assets/sword.vfMesh";
    jSword.hasSocketAttachment = true;
    jSword.attachParentEntityName = "Body";
    jSword.attachSocketName = "RightHandGrip";
    jBody.children.push_back(jSword);
    services::PrefabRigDescDTO json = windows::buildPrefabRigDescDTO(jBody);

    // desc.ik is now POPULATED (the regression was that it stayed empty -> "No IK chains").
    REQUIRE(live.desc.ik.size() == 1);
    REQUIRE(live.desc.ik.size() == json.ik.size());

    const services::PrefabRigIKDTO& lik = live.desc.ik[0];
    const services::PrefabRigIKDTO& jik = json.ik[0];
    CHECK(lik.chain.chainName == "LeftArm");
    CHECK(lik.chain.chainName == jik.chain.chainName);
    CHECK(lik.chain.tipBoneName == jik.chain.tipBoneName);
    CHECK(lik.chain.chainBoneNames == jik.chain.chainBoneNames);
    CHECK(lik.chain.weight == doctest::Approx(jik.chain.weight));
    CHECK(lik.chain.enabled == jik.chain.enabled);
    // Body part owns the chain; the first static part (the sword, index 1) is the default target.
    CHECK(lik.bodyPartIndex == 0);
    CHECK(lik.bodyPartIndex == jik.bodyPartIndex);
    CHECK(lik.targetPartIndex == 1);
    CHECK(lik.targetPartIndex == jik.targetPartIndex);
    CHECK(lik.targetSocketName.empty());

    // Hiding the static target leaves the body chain but removes the default target part.
    setActive(sword, false);
    windows::LiveRigBuildResult hiddenTarget = windows::buildPrefabRigDescFromEntity(body);
    REQUIRE(hiddenTarget.desc.parts.size() == 1);
    REQUIRE(hiddenTarget.desc.ik.size() == 1);
    CHECK(hiddenTarget.desc.ik[0].bodyPartIndex == 0);
    CHECK(hiddenTarget.desc.ik[0].targetPartIndex == -1);
    setActive(sword, true);
}

TEST_CASE("inactive non-root entity prunes a subtree, compacts indices, and restores on reactivation")
{
    store().clear();
    ensureFakeHandlers();

    // root (skeletal) -> A (static) -> A_child (static); plus B (static) directly under root.
    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");

    services::EntityHandle a = makeNode("A", makeXf({1, 0, 0}));
    setMesh(a, "assets/a.vfMesh");

    services::EntityHandle aChild = makeNode("AChild", makeXf({0, 1, 0}));
    setMesh(aChild, "assets/achild.vfMesh");

    services::EntityHandle b = makeNode("B", makeXf({-1, 0, 0}));
    setMesh(b, "assets/b.vfMesh");

    setParentChild(root, a);
    setParentChild(a, aChild);
    setParentChild(root, b);

    // No hide: 4 parts in DFS pre-order: Root, A, AChild, B.
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 4);
        CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");
        CHECK(r.desc.parts[1].meshPath == "assets/a.vfMesh");
        CHECK(r.desc.parts[2].meshPath == "assets/achild.vfMesh");
        CHECK(r.desc.parts[3].meshPath == "assets/b.vfMesh");
    }

    // Hide A by authored active state: A AND its descendant AChild drop; indices compact to Root, B.
    setActive(a, false);
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 2);
        CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");
        CHECK(r.desc.parts[1].meshPath == "assets/b.vfMesh"); // B compacted up to index 1
        REQUIRE(r.partEntities.size() == 2);
        CHECK(r.partEntities[0] == root);
        CHECK(r.partEntities[1] == b);
    }

    // Reactivating A restores all 4 parts.
    setActive(a, true);
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 4);
        CHECK(r.desc.parts[2].meshPath == "assets/achild.vfMesh");
    }
}

TEST_CASE("inactive sandbox root still yields active descendants")
{
    store().clear();
    ensureFakeHandlers();

    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle child = makeNode("Child", makeXf({1, 0, 0}));
    setMesh(child, "assets/child.vfMesh");
    setParentChild(root, child);

    // Mark the root inactive (this is exactly what MarkPreviewSandboxCommand does to the sandbox
    // root for isolation). The builder treats only the sandbox root as active by scope.
    setActive(root, false);

    windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
    REQUIRE(r.desc.parts.size() == 2);
    CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");
    CHECK(r.desc.parts[1].meshPath == "assets/child.vfMesh");

    // Restore for hygiene (other suites share the registry singleton).
    setActive(root, true);
}

// ---------------------------------------------------------------------------------------------
// P4aTest additions: deeper tree non-mesh-skip parity, name-resolved attach (incl. inactive parent
// fallback), multiple sockets, leaf vs mid-tree hide compaction, submesh-material parity, and the
// active-state pruning corner. Each derives its expected result from the two
// builders' code (PrefabRigLiveDescBuilder.hpp / PrefabRigDescBuilder.hpp) read above.
// ---------------------------------------------------------------------------------------------

// A 4-level tree with MULTIPLE mesh-bearing parts and MULTIPLE non-mesh intermediate nodes, to pin
// the DFS pre-order k-th-mesh-bearing index mapping against the JSON builder field-for-field AND to
// assert the accumulated localTransform matrices match (the field-equal helper omits that field).
//
//   Root(mesh, skel)
//   ├─ GroupA(no mesh)
//   │   ├─ Arm(mesh, static)              -> part 1
//   │   └─ GroupB(no mesh)
//   │       └─ Hand(mesh, static)         -> part 2
//   └─ Leg(mesh, static)                  -> part 3
// Pre-order visit: Root, GroupA, Arm, GroupB, Hand, Leg. Mesh nodes -> parts 0..3 in that order.
TEST_CASE("deep tree: non-mesh nodes are skipped identically to the JSON builder (parity + world)")
{
    store().clear();
    ensureFakeHandlers();

    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}, {0, 30, 0}, {1, 1, 1}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");

    services::EntityHandle groupA = makeNode("GroupA", makeXf({1, 0, 0}, {0, 0, 0}, {2, 2, 2}));
    services::EntityHandle arm = makeNode("Arm", makeXf({0, 2, 0}, {0, 0, 15}, {1, 1, 1}));
    setMesh(arm, "assets/arm.vfMesh");

    services::EntityHandle groupB = makeNode("GroupB", makeXf({0, 0, 3}, {45, 0, 0}, {1, 1, 1}));
    services::EntityHandle hand = makeNode("Hand", makeXf({0.5f, 0, 0}, {0, 90, 0}, {1, 1, 1}));
    setMesh(hand, "assets/hand.vfMesh");

    services::EntityHandle leg = makeNode("Leg", makeXf({0, -2, 0}, {0, 0, 0}, {1, 1, 1}));
    setMesh(leg, "assets/leg.vfMesh");

    setParentChild(root, groupA);
    setParentChild(groupA, arm);
    setParentChild(groupA, groupB);
    setParentChild(groupB, hand);
    setParentChild(root, leg);

    windows::LiveRigBuildResult live = windows::buildPrefabRigDescFromEntity(root);

    // Equivalent JSON tree.
    windows::PrefabEntityNode jRoot = jsonNode("Root", makeXf({0, 0, 0}, {0, 30, 0}, {1, 1, 1}));
    jRoot.meshPath = "assets/root.vfMesh";
    jRoot.animatorPath = "assets/root.vfAnimator";
    windows::PrefabEntityNode jGroupA = jsonNode("GroupA", makeXf({1, 0, 0}, {0, 0, 0}, {2, 2, 2}));
    windows::PrefabEntityNode jArm = jsonNode("Arm", makeXf({0, 2, 0}, {0, 0, 15}, {1, 1, 1}));
    jArm.meshPath = "assets/arm.vfMesh";
    windows::PrefabEntityNode jGroupB = jsonNode("GroupB", makeXf({0, 0, 3}, {45, 0, 0}, {1, 1, 1}));
    windows::PrefabEntityNode jHand = jsonNode("Hand", makeXf({0.5f, 0, 0}, {0, 90, 0}, {1, 1, 1}));
    jHand.meshPath = "assets/hand.vfMesh";
    windows::PrefabEntityNode jLeg = jsonNode("Leg", makeXf({0, -2, 0}, {0, 0, 0}, {1, 1, 1}));
    jLeg.meshPath = "assets/leg.vfMesh";

    jGroupB.children.push_back(jHand);
    jGroupA.children.push_back(jArm);
    jGroupA.children.push_back(jGroupB);
    jRoot.children.push_back(jGroupA);
    jRoot.children.push_back(jLeg);

    services::PrefabRigDescDTO json = windows::buildPrefabRigDescDTO(jRoot);

    REQUIRE(live.desc.parts.size() == 4);
    REQUIRE(json.parts.size() == 4);

    // The two non-mesh nodes (GroupA, GroupB) are skipped; the k-th-mesh mapping is identical.
    const char* expectMesh[4] = {"assets/root.vfMesh", "assets/arm.vfMesh", "assets/hand.vfMesh",
                                 "assets/leg.vfMesh"};
    for (size_t i = 0; i < live.desc.parts.size(); ++i)
    {
        CAPTURE(i);
        CHECK(dtoPartsFieldEqual(live.desc.parts[i], json.parts[i]));
        CHECK(live.desc.parts[i].meshPath == expectMesh[i]);
        // Accumulated world transform must match the JSON builder element-for-element. This proves
        // the live builder folds the SKIPPED non-mesh nodes' transforms (GroupA scale 2, GroupB
        // rotation/translation) into the descendant parts exactly like the JSON builder.
        CHECK(matEq(live.desc.parts[i].localTransform, json.parts[i].localTransform));
    }

    // Spot-check the deepest part's world has GroupA's scale folded in (translate(1,0,0) then
    // scale 2 means Hand's accumulated origin is well past its own 0.5 local x).
    REQUIRE(live.partEntities.size() == 4);
    CHECK(live.partEntities[0] == root);
    CHECK(live.partEntities[1] == arm);
    CHECK(live.partEntities[2] == hand);
    CHECK(live.partEntities[3] == leg);
}

// Multiple socketed parts, each resolving its parent by NAME against a different mesh part, plus a
// part whose named parent is a NON-MESH node (must fall back to root, parentPartIndex == -1).
TEST_CASE("name-resolved socket attach: multiple parts + parent-without-a-part falls back to root")
{
    store().clear();
    ensureFakeHandlers();

    // Body(skel) -> Torso(static) ; Sword socketed to "Body", Shield socketed to "Torso",
    // Cape socketed to "Decoration" (a non-mesh node => no part => fall back to root).
    services::EntityHandle body = makeNode("Body", makeXf({0, 0, 0}));
    setMesh(body, "assets/body.vfMesh", "assets/body.vfAnimator");

    services::EntityHandle torso = makeNode("Torso", makeXf({0, 1, 0}));
    setMesh(torso, "assets/torso.vfMesh");

    services::EntityHandle deco = makeNode("Decoration", makeXf({0, 0, 0})); // no mesh

    services::EntityHandle sword = makeNode("Sword", makeXf({0, 0, 0}, {0, 0, 10}, {1, 1, 1}));
    setMesh(sword, "assets/sword.vfMesh");
    setSocket(sword, "Body", "RightHand");

    services::EntityHandle shield = makeNode("Shield", makeXf({0, 0, 0}, {0, 0, 20}, {1, 1, 1}));
    setMesh(shield, "assets/shield.vfMesh");
    setSocket(shield, "Torso", "LeftHand");

    services::EntityHandle cape = makeNode("Cape", makeXf({0, 0, 0}, {0, 0, 30}, {1, 1, 1}));
    setMesh(cape, "assets/cape.vfMesh");
    setSocket(cape, "Decoration", "Back"); // parent is a non-mesh node

    setParentChild(body, torso);
    setParentChild(body, deco);
    setParentChild(body, sword);
    setParentChild(body, shield);
    setParentChild(body, cape);

    windows::LiveRigBuildResult live = windows::buildPrefabRigDescFromEntity(body);

    // Pre-order mesh parts: Body(0), Torso(1), Sword(2), Shield(3), Cape(4). (Decoration skipped.)
    REQUIRE(live.desc.parts.size() == 5);
    CHECK(live.desc.parts[0].meshPath == "assets/body.vfMesh");
    CHECK(live.desc.parts[0].parentPartIndex == -1);
    CHECK(live.desc.parts[1].meshPath == "assets/torso.vfMesh");
    CHECK(live.desc.parts[1].parentPartIndex == -1); // no socket on Torso

    // Sword -> Body (part 0).
    CHECK(live.desc.parts[2].meshPath == "assets/sword.vfMesh");
    CHECK(live.desc.parts[2].parentPartIndex == 0);
    CHECK(live.desc.parts[2].attachParentSocket == "RightHand");

    // Shield -> Torso (part 1).
    CHECK(live.desc.parts[3].meshPath == "assets/shield.vfMesh");
    CHECK(live.desc.parts[3].parentPartIndex == 1);
    CHECK(live.desc.parts[3].attachParentSocket == "LeftHand");

    // Cape -> "Decoration" has no mesh part: falls back to root (parentPartIndex == -1) and leaves
    // attachParentSocket empty, matching the JSON builder's `nameToPart.end()` continue.
    CHECK(live.desc.parts[4].meshPath == "assets/cape.vfMesh");
    CHECK(live.desc.parts[4].parentPartIndex == -1);
    CHECK(live.desc.parts[4].attachParentSocket.empty());

    // Field-for-field parity against the JSON builder for the same logical tree.
    windows::PrefabEntityNode jBody = jsonNode("Body", makeXf({0, 0, 0}));
    jBody.meshPath = "assets/body.vfMesh";
    jBody.animatorPath = "assets/body.vfAnimator";
    windows::PrefabEntityNode jTorso = jsonNode("Torso", makeXf({0, 1, 0}));
    jTorso.meshPath = "assets/torso.vfMesh";
    windows::PrefabEntityNode jDeco = jsonNode("Decoration", makeXf({0, 0, 0}));
    windows::PrefabEntityNode jSword = jsonNode("Sword", makeXf({0, 0, 0}, {0, 0, 10}, {1, 1, 1}));
    jSword.meshPath = "assets/sword.vfMesh";
    jSword.hasSocketAttachment = true;
    jSword.attachParentEntityName = "Body";
    jSword.attachSocketName = "RightHand";
    windows::PrefabEntityNode jShield = jsonNode("Shield", makeXf({0, 0, 0}, {0, 0, 20}, {1, 1, 1}));
    jShield.meshPath = "assets/shield.vfMesh";
    jShield.hasSocketAttachment = true;
    jShield.attachParentEntityName = "Torso";
    jShield.attachSocketName = "LeftHand";
    windows::PrefabEntityNode jCape = jsonNode("Cape", makeXf({0, 0, 0}, {0, 0, 30}, {1, 1, 1}));
    jCape.meshPath = "assets/cape.vfMesh";
    jCape.hasSocketAttachment = true;
    jCape.attachParentEntityName = "Decoration";
    jCape.attachSocketName = "Back";
    jBody.children.push_back(jTorso);
    jBody.children.push_back(jDeco);
    jBody.children.push_back(jSword);
    jBody.children.push_back(jShield);
    jBody.children.push_back(jCape);

    services::PrefabRigDescDTO json = windows::buildPrefabRigDescDTO(jBody);
    REQUIRE(json.parts.size() == live.desc.parts.size());
    for (size_t i = 0; i < live.desc.parts.size(); ++i)
    {
        CAPTURE(i);
        CHECK(dtoPartsFieldEqual(live.desc.parts[i], json.parts[i]));
    }
}

// Inactivating the socket-attach PARENT makes the child fall back to root: with the parent's mesh node
// pruned, it is absent from nameToPart, so the child's parentPartIndex resolves to -1 (matching the
// JSON builder's behavior when a named parent has no part). Distinct from the no-part case above:
// here the parent IS a mesh node, inactive only in the authored hierarchy.
TEST_CASE("inactive attach-parent: socketed child falls back to root (no parent part)")
{
    store().clear();
    ensureFakeHandlers();

    // Root(skel) -> Holder(static) -> Gem(static, socketed to "Holder").
    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle holder = makeNode("Holder", makeXf({0, 1, 0}));
    setMesh(holder, "assets/holder.vfMesh");
    services::EntityHandle gem = makeNode("Gem", makeXf({0, 0, 0}, {0, 0, 5}, {1, 1, 1}));
    setMesh(gem, "assets/gem.vfMesh");
    setSocket(gem, "Holder", "Mount");
    setParentChild(root, holder);
    setParentChild(holder, gem);

    // Baseline: Gem attaches to Holder (part 1).
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 3);
        CHECK(r.desc.parts[2].meshPath == "assets/gem.vfMesh");
        CHECK(r.desc.parts[2].parentPartIndex == 1);
        CHECK(r.desc.parts[2].attachParentSocket == "Mount");
    }

    // Hide Holder. Note Gem is a CHILD of Holder in the tree, so pruning Holder also prunes Gem.
    // To isolate "parent has no part but child still present", we instead make Gem a sibling of
    // Holder (re-parented under Root) so hiding Holder removes only Holder.
    store().nodes[holder.id].children.clear(); // detach Gem from Holder
    setParentChild(root, gem);                 // Gem now a direct child of Root, still socketed to "Holder"

    setActive(holder, false);
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        // Parts: Root(0), Gem(1). Holder pruned.
        REQUIRE(r.desc.parts.size() == 2);
        CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");
        CHECK(r.desc.parts[1].meshPath == "assets/gem.vfMesh");
        // "Holder" no longer in nameToPart -> Gem falls back to root.
        CHECK(r.desc.parts[1].parentPartIndex == -1);
        CHECK(r.desc.parts[1].attachParentSocket.empty());
    }

    // Unhide Holder: Gem re-resolves to Holder. (Holder is now visited AFTER Gem in pre-order
    // since Gem was reparented first; name resolution is a second pass over all parts, so order
    // does not matter — assert the link is restored.)
    setActive(holder, true);
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 3);
        // Find Gem + Holder part indices by mesh path (pre-order is Root, Holder?, Gem? — Gem was
        // reparented before Holder's subtree is gone, so order is Root, Gem, Holder).
        int gemIdx = -1, holderIdx = -1;
        for (size_t i = 0; i < r.desc.parts.size(); ++i)
        {
            if (r.desc.parts[i].meshPath == "assets/gem.vfMesh") gemIdx = (int)i;
            if (r.desc.parts[i].meshPath == "assets/holder.vfMesh") holderIdx = (int)i;
        }
        REQUIRE(gemIdx >= 0);
        REQUIRE(holderIdx >= 0);
        CHECK(r.desc.parts[gemIdx].parentPartIndex == holderIdx);
        CHECK(r.desc.parts[gemIdx].attachParentSocket == "Mount");
    }
}

// Hiding a LEAF mesh node compacts indices the same way a mid-tree hide does, and partEntities stays
// parallel. Complements the existing mid-tree hide case.
TEST_CASE("hide a leaf mesh node: indices compact, partEntities stays parallel")
{
    store().clear();
    ensureFakeHandlers();

    // Root(skel) -> M1(static) -> M2(static, leaf); plus M3(static, leaf under root).
    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle m1 = makeNode("M1", makeXf({1, 0, 0}));
    setMesh(m1, "assets/m1.vfMesh");
    services::EntityHandle m2 = makeNode("M2", makeXf({0, 1, 0}));
    setMesh(m2, "assets/m2.vfMesh");
    services::EntityHandle m3 = makeNode("M3", makeXf({-1, 0, 0}));
    setMesh(m3, "assets/m3.vfMesh");
    setParentChild(root, m1);
    setParentChild(m1, m2);
    setParentChild(root, m3);

    // No hide: Root, M1, M2, M3.
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 4);
        REQUIRE(r.partEntities.size() == 4);
        CHECK(r.partEntities[2] == m2);
        CHECK(r.partEntities[3] == m3);
    }

    // Hide the LEAF M2 only: M1 (its parent) stays. Parts compact to Root, M1, M3.
    setActive(m2, false);
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 3);
        CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");
        CHECK(r.desc.parts[1].meshPath == "assets/m1.vfMesh");
        CHECK(r.desc.parts[2].meshPath == "assets/m3.vfMesh"); // M3 compacted from index 3 -> 2
        REQUIRE(r.partEntities.size() == 3);
        CHECK(r.partEntities[0] == root);
        CHECK(r.partEntities[1] == m1);
        CHECK(r.partEntities[2] == m3); // partEntities parallel to compacted parts
    }
    setActive(m2, true);
}

// subMeshMaterials are read from the MaterialComponent and carried per-part exactly as the JSON
// builder copies n.subMeshMaterials. (The base parity helper omits this field, so assert it here.)
TEST_CASE("submesh material overrides are carried per-part (parity with JSON builder)")
{
    store().clear();
    ensureFakeHandlers();

    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    setMaterial(root, "assets/base.vfMaterial");
    setSubMeshMaterial(root, "Head", "assets/head.vfMaterial");
    setSubMeshMaterial(root, "Body", "assets/body.vfMaterial");

    windows::LiveRigBuildResult live = windows::buildPrefabRigDescFromEntity(root);
    REQUIRE(live.desc.parts.size() == 1);
    const auto& part = live.desc.parts[0];
    CHECK(part.defaultMaterialPath == "assets/base.vfMaterial");
    REQUIRE(part.subMeshMaterials.size() == 2);
    CHECK(part.subMeshMaterials.at("Head") == "assets/head.vfMaterial");
    CHECK(part.subMeshMaterials.at("Body") == "assets/body.vfMaterial");

    // JSON-builder parity for the submesh map.
    windows::PrefabEntityNode jRoot = jsonNode("Root", makeXf({0, 0, 0}));
    jRoot.meshPath = "assets/root.vfMesh";
    jRoot.animatorPath = "assets/root.vfAnimator";
    jRoot.defaultMaterialPath = "assets/base.vfMaterial";
    jRoot.subMeshMaterials["Head"] = "assets/head.vfMaterial";
    jRoot.subMeshMaterials["Body"] = "assets/body.vfMaterial";
    services::PrefabRigDescDTO json = windows::buildPrefabRigDescDTO(jRoot);
    REQUIRE(json.parts.size() == 1);
    CHECK(part.subMeshMaterials == json.parts[0].subMeshMaterials);
}

// Active-state visibility: the sandbox root's inactive flag is ignored, but an inactive interior
// child prunes that child and its descendants.
TEST_CASE("inactive root is scoped active but inactive child prunes its subtree")
{
    store().clear();
    ensureFakeHandlers();

    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle child = makeNode("Child", makeXf({1, 0, 0}));
    setMesh(child, "assets/child.vfMesh");
    services::EntityHandle grand = makeNode("Grand", makeXf({0, 1, 0}));
    setMesh(grand, "assets/grand.vfMesh");
    setParentChild(root, child);
    setParentChild(child, grand);

    setActive(root, false);
    setActive(child, false); // an interior node inactive too

    windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
    REQUIRE(r.desc.parts.size() == 1);
    CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");

    // Restore for hygiene.
    setActive(root, true);
    setActive(child, true);
}

// Subtree-scoped duplicate names: when two mesh nodes share a name, "last node wins" in nameToPart
// (both builders), so a socket attach to that name resolves to the LAST such part.
TEST_CASE("duplicate part names: socket resolves to the last-visited part (parity)")
{
    store().clear();
    ensureFakeHandlers();

    // Root(skel) -> Hand(static, first) ; Hand(static, second) ; Ring socketed to "Hand".
    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle hand1 = makeNode("Hand", makeXf({1, 0, 0}));
    setMesh(hand1, "assets/hand1.vfMesh");
    services::EntityHandle hand2 = makeNode("Hand", makeXf({-1, 0, 0}));
    setMesh(hand2, "assets/hand2.vfMesh");
    services::EntityHandle ring = makeNode("Ring", makeXf({0, 0, 0}));
    setMesh(ring, "assets/ring.vfMesh");
    setSocket(ring, "Hand", "Finger");
    setParentChild(root, hand1);
    setParentChild(root, hand2);
    setParentChild(root, ring);

    windows::LiveRigBuildResult live = windows::buildPrefabRigDescFromEntity(root);
    // Parts pre-order: Root(0), Hand1(1), Hand2(2), Ring(3). nameToPart["Hand"] = 2 (last wins).
    REQUIRE(live.desc.parts.size() == 4);
    CHECK(live.desc.parts[3].meshPath == "assets/ring.vfMesh");
    CHECK(live.desc.parts[3].parentPartIndex == 2); // resolves to the SECOND "Hand"
    CHECK(live.desc.parts[3].attachParentSocket == "Finger");

    // JSON-builder parity.
    windows::PrefabEntityNode jRoot = jsonNode("Root", makeXf({0, 0, 0}));
    jRoot.meshPath = "assets/root.vfMesh";
    jRoot.animatorPath = "assets/root.vfAnimator";
    windows::PrefabEntityNode jHand1 = jsonNode("Hand", makeXf({1, 0, 0}));
    jHand1.meshPath = "assets/hand1.vfMesh";
    windows::PrefabEntityNode jHand2 = jsonNode("Hand", makeXf({-1, 0, 0}));
    jHand2.meshPath = "assets/hand2.vfMesh";
    windows::PrefabEntityNode jRing = jsonNode("Ring", makeXf({0, 0, 0}));
    jRing.meshPath = "assets/ring.vfMesh";
    jRing.hasSocketAttachment = true;
    jRing.attachParentEntityName = "Hand";
    jRing.attachSocketName = "Finger";
    jRoot.children.push_back(jHand1);
    jRoot.children.push_back(jHand2);
    jRoot.children.push_back(jRing);
    services::PrefabRigDescDTO json = windows::buildPrefabRigDescDTO(jRoot);
    REQUIRE(json.parts.size() == 4);
    CHECK(json.parts[3].parentPartIndex == 2);
    for (size_t i = 0; i < live.desc.parts.size(); ++i)
    {
        CAPTURE(i);
        CHECK(dtoPartsFieldEqual(live.desc.parts[i], json.parts[i]));
    }
}

// =============================================================================================
// VK-1433 Phase 4b/4c — mutation CQRS drives the rig re-derive (the window's spine). These exercise
// the exact commands the window issues against the fake store, then re-derive and assert the edit
// landed: create + set-mesh, delete (subtree + index compaction), rename-of-attach-parent (link ->
// root, the documented O2 behavior), reparent + reorder (DFS index remap + partEntities parallel),
// active-state hide, and the entity-driven save roundtrip (4c spine regression).
// =============================================================================================

namespace
{
    using D = events::EventDispatcher;

    // Mimic the window's applyAssetDropToPart .vfMesh path: SetMeshData carrying a resolved meshRef.
    void dispatchSetMesh(services::EntityHandle e, const std::string& meshPath,
                         const std::string& animatorPath = {})
    {
        events::scene::SetMeshDataCommand cmd;
        cmd.entity = e;
        cmd.meshData.meshRef = refFor(meshPath, resource::AssetType::Mesh);
        if (!animatorPath.empty())
            cmd.meshData.animatorRef = refFor(animatorPath, resource::AssetType::Animator);
        D::instance().execute(cmd);
    }
}

TEST_CASE("4b: create child + set-mesh adds a part; delete a mid-tree mesh node compacts indices")
{
    store().clear();
    ensureFakeHandlers();

    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle a = makeNode("A", makeXf({1, 0, 0}));
    setMesh(a, "assets/a.vfMesh");
    setParentChild(root, a);

    // Baseline: Root, A.
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 2);
    }

    // Create a child under Root via CQRS, then give it a mesh — it becomes a new part.
    events::scene::CreateEntityCommand create;
    create.name = "B";
    create.parent = root;
    services::EntityHandle b = D::instance().execute(create);
    REQUIRE(b.isValid());
    dispatchSetMesh(b, "assets/b.vfMesh");

    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        // DFS pre-order: Root(0), A(1), B(2).
        REQUIRE(r.desc.parts.size() == 3);
        CHECK(r.desc.parts[2].meshPath == "assets/b.vfMesh");
        CHECK(r.partEntities[2] == b);
    }

    // Delete the mid-tree mesh node A: its part drops, B compacts up to index 1.
    events::scene::DeleteEntityCommand del;
    del.entity = a;
    CHECK(D::instance().execute(del));
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 2);
        CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");
        CHECK(r.desc.parts[1].meshPath == "assets/b.vfMesh"); // compacted from 2 -> 1
        CHECK(r.partEntities[1] == b);
    }
}

TEST_CASE("4b: deleting a subtree root drops the whole subtree")
{
    store().clear();
    ensureFakeHandlers();

    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle a = makeNode("A", makeXf({1, 0, 0}));
    setMesh(a, "assets/a.vfMesh");
    services::EntityHandle aChild = makeNode("AChild", makeXf({0, 1, 0}));
    setMesh(aChild, "assets/achild.vfMesh");
    setParentChild(root, a);
    setParentChild(a, aChild);

    events::scene::DeleteEntityCommand del;
    del.entity = a; // deletes A and AChild
    CHECK(D::instance().execute(del));

    windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
    REQUIRE(r.desc.parts.size() == 1);
    CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");
}

TEST_CASE("4b: renaming a socket-attach parent drops the child's link to root (O2 documented behavior)")
{
    store().clear();
    ensureFakeHandlers();

    // Root(skel) -> Hand(static) ; Sword socketed to "Hand" (sibling of Hand under Root).
    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle hand = makeNode("Hand", makeXf({1, 0, 0}));
    setMesh(hand, "assets/hand.vfMesh");
    services::EntityHandle sword = makeNode("Sword", makeXf({0, 0, 0}));
    setMesh(sword, "assets/sword.vfMesh");
    setSocket(sword, "Hand", "Grip");
    setParentChild(root, hand);
    setParentChild(root, sword);

    // Baseline: Sword attaches to Hand (part 1).
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 3);
        CHECK(r.desc.parts[2].meshPath == "assets/sword.vfMesh");
        CHECK(r.desc.parts[2].parentPartIndex == 1);
        CHECK(r.desc.parts[2].attachParentSocket == "Grip");
    }

    // Rename "Hand" -> "Palm". The socket still resolves BY NAME against "Hand", which no longer
    // exists -> the link drops to root (parentPartIndex -1, empty socket). This is the WARN case.
    events::scene::SetEntityNameCommand rename;
    rename.entity = hand;
    rename.newName = "Palm";
    D::instance().execute(rename);

    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 3);
        int swordIdx = -1;
        for (size_t i = 0; i < r.desc.parts.size(); ++i)
            if (r.desc.parts[i].meshPath == "assets/sword.vfMesh") swordIdx = (int)i;
        REQUIRE(swordIdx >= 0);
        CHECK(r.desc.parts[swordIdx].parentPartIndex == -1); // link dropped to root
        CHECK(r.desc.parts[swordIdx].attachParentSocket.empty());
    }

    // Re-pointing the socket to the new name restores the link (proves it's a name-resolution drop,
    // not data loss).
    setSocket(sword, "Palm", "Grip");
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        int swordIdx = -1, palmIdx = -1;
        for (size_t i = 0; i < r.desc.parts.size(); ++i)
        {
            if (r.desc.parts[i].meshPath == "assets/sword.vfMesh") swordIdx = (int)i;
            if (r.desc.parts[i].meshPath == "assets/hand.vfMesh") palmIdx = (int)i;
        }
        REQUIRE(swordIdx >= 0);
        REQUIRE(palmIdx >= 0);
        CHECK(r.desc.parts[swordIdx].parentPartIndex == palmIdx);
    }
}

TEST_CASE("4b: reparent + reorder remap DFS part indices, partEntities stays parallel")
{
    store().clear();
    ensureFakeHandlers();

    // Root(skel) -> A(static), B(static), C(static) in that order.
    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle a = makeNode("A", makeXf({1, 0, 0}));
    setMesh(a, "assets/a.vfMesh");
    services::EntityHandle b = makeNode("B", makeXf({2, 0, 0}));
    setMesh(b, "assets/b.vfMesh");
    services::EntityHandle c = makeNode("C", makeXf({3, 0, 0}));
    setMesh(c, "assets/c.vfMesh");
    setParentChild(root, a);
    setParentChild(root, b);
    setParentChild(root, c);

    // Baseline DFS: Root(0), A(1), B(2), C(3).
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 4);
        CHECK(r.partEntities[1] == a);
        CHECK(r.partEntities[2] == b);
        CHECK(r.partEntities[3] == c);
    }

    // Reparent C under A. DFS pre-order is now Root, A, C, B -> parts Root(0), A(1), C(2), B(3).
    events::scene::ReparentEntityCommand reparent;
    reparent.entity = c;
    reparent.newParent = a;
    CHECK(D::instance().execute(reparent));
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 4);
        CHECK(r.desc.parts[1].meshPath == "assets/a.vfMesh");
        CHECK(r.desc.parts[2].meshPath == "assets/c.vfMesh"); // C now visited right after A
        CHECK(r.desc.parts[3].meshPath == "assets/b.vfMesh");
        CHECK(r.partEntities[2] == c);
        CHECK(r.partEntities[3] == b);
    }

    // Reorder B to the FRONT of Root's children (insertIndex 0). DFS: Root, B, A, C ->
    // parts Root(0), B(1), A(2), C(3).
    events::scene::ReorderEntityCommand reorder;
    reorder.entity = b;
    reorder.newParent = root;
    reorder.insertIndex = 0;
    CHECK(D::instance().execute(reorder));
    {
        windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
        REQUIRE(r.desc.parts.size() == 4);
        CHECK(r.desc.parts[1].meshPath == "assets/b.vfMesh");
        CHECK(r.desc.parts[2].meshPath == "assets/a.vfMesh");
        CHECK(r.desc.parts[3].meshPath == "assets/c.vfMesh"); // C still rides under A
        CHECK(r.partEntities[1] == b);
        CHECK(r.partEntities[2] == a);
        CHECK(r.partEntities[3] == c);
    }
}

TEST_CASE("4b: hide via SetEntityActiveCommand prunes the part and persists isActive")
{
    store().clear();
    ensureFakeHandlers();

    services::EntityHandle root = makeNode("Root", makeXf({0, 0, 0}));
    setMesh(root, "assets/root.vfMesh", "assets/root.vfAnimator");
    services::EntityHandle a = makeNode("A", makeXf({1, 0, 0}));
    setMesh(a, "assets/a.vfMesh");
    setParentChild(root, a);

    // Hide A through the same CQRS command the prefab-view eye button issues.
    events::scene::SetEntityActiveCommand hide;
    hide.entity = a;
    hide.isActive = false;
    D::instance().execute(hide);

    windows::LiveRigBuildResult r = windows::buildPrefabRigDescFromEntity(root);
    REQUIRE(r.desc.parts.size() == 1);
    CHECK(r.desc.parts[0].meshPath == "assets/root.vfMesh");

    // Unhide restores it.
    events::scene::SetEntityActiveCommand show;
    show.entity = a;
    show.isActive = true;
    D::instance().execute(show);

    windows::LiveRigBuildResult r2 = windows::buildPrefabRigDescFromEntity(root);
    REQUIRE(r2.desc.parts.size() == 2);
}

// 4c SAVE ROUNDTRIP — the strongest spine regression. The window edits the source ENTITY transform
// (gizmo / numeric fields) and mesh ref (drag-swap), then SavePrefab serializes those entities. The
// live builder reads exactly the same component data the serializer writes, so a re-derive AFTER a
// simulated save->reload (deep-copy the store subtree into fresh ids) must equal the pre-save DTO:
// the transforms + refs round-trip and the rig is stable under entity-id renaming.
TEST_CASE("4c: entity-driven edits survive a save->reload re-derive (DTO equality)")
{
    store().clear();
    ensureFakeHandlers();

    // Body(skel) -> Sword(static, socketed to Body).
    services::EntityHandle body = makeNode("Body", makeXf({0, 0, 0}, {0, 90, 0}, {1, 1, 1}));
    setMesh(body, "assets/body.vfMesh", "assets/body.vfAnimator");
    setMaterial(body, "assets/skin.vfMaterial");
    services::EntityHandle sword = makeNode("Sword", makeXf({0, 0, 0}));
    setMesh(sword, "assets/sword.vfMesh");
    setSocket(sword, "Body", "Grip");
    setParentChild(body, sword);

    // Edit via the SAME CQRS the window issues: move/rotate the sword entity (gizmo path) and swap
    // the body's mesh (drag-swap path).
    {
        events::scene::SetTransformCommand t;
        t.entity = sword;
        t.transform = makeXf({0, 0, 0}, {0, 0, 45}, {2, 2, 2});
        D::instance().execute(t);
    }
    dispatchSetMesh(body, "assets/body_v2.vfMesh", "assets/body.vfAnimator");

    // Pre-save DTO derived from the live (edited) entities.
    windows::LiveRigBuildResult preSave = windows::buildPrefabRigDescFromEntity(body);
    REQUIRE(preSave.desc.parts.size() == 2);
    CHECK(preSave.desc.parts[0].meshPath == "assets/body_v2.vfMesh");           // mesh swap landed
    CHECK(preSave.desc.parts[1].attachChildRotation.z == doctest::Approx(45.0f)); // transform landed
    CHECK(preSave.desc.parts[1].attachChildScale.x == doctest::Approx(2.0f));

    // Simulate SavePrefab -> reload: deep-copy the body subtree into a fresh set of entity ids (the
    // serializer writes the entities' component data; the reloaded prefab is the same data under new
    // ids). The re-derive must produce an equal DTO.
    std::unordered_map<uint64_t, services::EntityHandle> remap;
    std::function<services::EntityHandle(services::EntityHandle)> deepCopy =
        [&](services::EntityHandle src) -> services::EntityHandle
    {
        const FakeNode original = store().nodes.at(src.id); // copy (children reassigned below)
        const services::EntityHandle dst{store().nextId++};
        remap[src.id] = dst;
        FakeNode copy = original;
        copy.children.clear();
        store().nodes[dst.id] = copy;
        for (auto child : original.children)
            store().nodes[dst.id].children.push_back(deepCopy(child));
        return dst;
    };
    const services::EntityHandle reloadedRoot = deepCopy(body);

    windows::LiveRigBuildResult postReload = windows::buildPrefabRigDescFromEntity(reloadedRoot);

    // Same part count + field-equal parts (mesh/animator/material paths, parent links, attach rot/
    // scale) + equal accumulated world transforms. The only thing that differs is the entity ids in
    // partEntities, which we deliberately do NOT compare.
    REQUIRE(postReload.desc.parts.size() == preSave.desc.parts.size());
    for (size_t i = 0; i < preSave.desc.parts.size(); ++i)
    {
        CAPTURE(i);
        CHECK(dtoPartsFieldEqual(postReload.desc.parts[i], preSave.desc.parts[i]));
        CHECK(matEq(postReload.desc.parts[i].localTransform, preSave.desc.parts[i].localTransform));
    }
    // The edited mesh ref + child transform round-tripped through the save->reload.
    CHECK(postReload.desc.parts[0].meshPath == "assets/body_v2.vfMesh");
    CHECK(postReload.desc.parts[1].attachChildRotation.z == doctest::Approx(45.0f));
    CHECK(postReload.desc.parts[1].attachChildScale.x == doctest::Approx(2.0f));
}

} // TEST_SUITE("PrefabRigLiveDescBuilder")

// =================================================================================================
// VK-1433 Phase 4c (P4bcdTest) — the REAL save->file->reload roundtrip through the production
// serializer (serialization::PrefabSerialization::savePrefab / loadPrefab — the exact body the
// window's SavePrefabCommand / LoadPrefabCommand handlers call). Distinct from the 4c case above,
// which only DEEP-COPIES the fake store into fresh ids. Here we:
//   1. build a REAL scene::Entity sandbox subtree in the live registry (mesh + socket + IK + xform),
//   2. tag the root PreviewSandboxTagComponent and hold it INACTIVE (exactly the window's isolation),
//   3. re-derive the pre-save DTO via buildPrefabRigDescFromEntity (registry-fallback handlers),
//   4. savePrefab to a temp .vfPrefab, then assert ON THE SAVED FILE that the spine normalization
//      held: the PreviewSandboxTag is ABSENT (editor-only, never serialized) and the root's
//      isActive is normalized to TRUE (PrefabSerialization.cpp:339-346 — a sandbox-inactive root must
//      not bake invisible),
//   5. loadPrefab the file back into a fresh SceneGraphSystem and re-derive on the reloaded root,
//      asserting the DTO is field-equal to pre-save AND the reloaded root is active with no tag.
// This is the strongest spine regression for the hybrid-B save path — it pins the actual serializer,
// not a hand-rolled copy.
// =================================================================================================
namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path roundtripTestDir()
    {
        return fs::temp_directory_path() / "vf_prefab_rig_save_roundtrip_tests";
    }

    // Build a real scene::Entity with Name/Transform set; optionally tag it as the sandbox root and
    // hold it inactive (mirrors MarkPreviewSandboxCommand). Caller wires children via addChild.
    scene::Entity makeRealNode(const std::string& name, const services::TransformData& xf,
                               bool sandboxRoot = false)
    {
        scene::Entity e(name);
        auto& t = e.getComponent<components::TransformComponent>();
        t.position = xf.position;
        t.rotation = xf.rotation;
        t.scale = xf.scale;
        if (sandboxRoot)
        {
            // PreviewSandboxTagComponent is an empty tag; Entity::addComponent can't bind a void&
            // for a fields-less type, so emplace via the registry directly (mirrors how
            // test_ui_preview_isolation.cpp tags UIPreviewTagComponent).
            scene::EntityRegistry::getRegistry().emplace<components::PreviewSandboxTagComponent>(e.getHandle());
            e.getComponent<components::NameComponent>().isActive = false; // isolation: main passes skip it
        }
        return e;
    }

    void setRealMesh(scene::Entity& e, const std::string& mesh, const std::string& animator = {})
    {
        auto& m = e.addComponent<components::MeshComponent>();
        m.meshRef = refFor(mesh, resource::AssetType::Mesh);
        if (!animator.empty())
            m.animatorRef = refFor(animator, resource::AssetType::Animator);
    }

    void setRealSocket(scene::Entity& e, const std::string& parentName, const std::string& socketName)
    {
        auto& s = e.addComponent<components::SocketAttachmentComponent>();
        s.parentEntityName = parentName;
        s.socketName = socketName;
    }

    // Recursively destroy a real subtree (registry hygiene — the registry is a process singleton).
    void destroyRealSubtree(scene::Entity e)
    {
        if (!e.isAlive()) return;
        for (scene::Entity child : e.getChildren())
            destroyRealSubtree(child);
        scene::EntityRegistry::getRegistry().destroy(e.getHandle());
    }

    // Collect every "name" anywhere in a serialized entity-JSON subtree (the prefab "entity" payload).
    void collectPrefabNames(const json& node, std::vector<std::string>& out)
    {
        if (node.contains("name") && node["name"].is_string())
            out.push_back(node["name"].get<std::string>());
        if (node.contains("children") && node["children"].is_array())
            for (const auto& child : node["children"])
                collectPrefabNames(child, out);
    }
}

TEST_SUITE("PrefabRigSaveRoundtrip")
{

TEST_CASE("4c: REAL savePrefab->loadPrefab re-derive equals pre-save DTO; tag stripped + isActive normalized")
{
    store().clear();
    ensureFakeHandlers();
    asset::AssetDatabase::instance().clear();
    std::error_code ec;
    fs::create_directories(roundtripTestDir(), ec);

    // --- Real sandbox subtree: Body(skeletal, sandbox root, held inactive) -> Sword(static, socketed)
    scene::Entity body = makeRealNode("Body", makeXf({0, 0, 0}, {0, 90, 0}, {1, 1, 1}), /*sandboxRoot*/ true);
    setRealMesh(body, "assets/body.vfMesh", "assets/body.vfAnimator");
    {
        auto& mat = body.addComponent<components::MaterialComponent>();
        mat.defaultMaterialRef = refFor("assets/skin.vfMaterial", resource::AssetType::Material);
    }

    scene::Entity sword = makeRealNode("Sword", makeXf({0, 0, 0}, {0, 0, 45}, {2, 2, 2}));
    setRealMesh(sword, "assets/sword.vfMesh"); // static (no animator)
    setRealSocket(sword, "Body", "RightHandGrip");
    // An IK chain on the body so desc.ik is exercised through the real roundtrip.
    {
        auto& ik = body.addComponent<components::IKTargetComponent>();
        ik.chains.push_back(makeChain("LeftArm", "Hand", {"Shoulder", "Elbow"}, 0.8f, true));
    }
    body.addChildren(sword);

    // Pre-save DTO from the LIVE (tagged, inactive) sandbox — derived exactly as the open window does.
    windows::LiveRigBuildResult preSave = windows::buildPrefabRigDescFromEntity(
        services::internal::toHandle(body.getHandle()));
    REQUIRE(preSave.desc.parts.size() == 2);
    CHECK(preSave.desc.parts[0].meshPath == "assets/body.vfMesh");
    CHECK(preSave.desc.parts[1].meshPath == "assets/sword.vfMesh");
    CHECK(preSave.desc.parts[1].parentPartIndex == 0);
    CHECK(preSave.desc.parts[1].attachParentSocket == "RightHandGrip");
    REQUIRE(preSave.desc.ik.size() == 1);
    CHECK(preSave.desc.ik[0].chain.chainName == "LeftArm");

    // Sanity: the live sandbox root really is inactive + tagged before save (the isolation state).
    CHECK_FALSE(body.getComponent<components::NameComponent>().isActive);
    CHECK(body.hasComponent<components::PreviewSandboxTagComponent>());

    // --- REAL save through the production serializer (== SavePrefabCommand body). ---
    const fs::path prefabPath = roundtripTestDir() / "RigRoundtrip.vfPrefab";
    REQUIRE(serialization::PrefabSerialization::savePrefab(body, prefabPath.string()));

    // Assert ON THE SAVED FILE: the editor-only sandbox tag never bakes, and the inactive sandbox
    // root is NORMALIZED to isActive=true (PrefabSerialization.cpp:339-346) so it instantiates visible.
    {
        std::ifstream file(prefabPath);
        REQUIRE(file.is_open());
        json prefabJson;
        file >> prefabJson;
        REQUIRE(prefabJson.contains("prefab"));
        REQUIRE(prefabJson["prefab"].contains("entity"));
        const json& rootJson = prefabJson["prefab"]["entity"];

        // (a) isActive normalized to true on the saved root despite the live inactive flag.
        REQUIRE(rootJson.contains("isActive"));
        CHECK(rootJson["isActive"].get<bool>() == true);

        // (b) the PreviewSandboxTag is editor-only — no component key for it anywhere in the payload.
        const std::string dumped = prefabJson.dump();
        CHECK(dumped.find("PreviewSandboxTag") == std::string::npos);
        CHECK(dumped.find("previewSandbox") == std::string::npos);

        // The rig nodes themselves persisted (Body + Sword).
        std::vector<std::string> names;
        collectPrefabNames(rootJson, names);
        CHECK(std::find(names.begin(), names.end(), "Body") != names.end());
        CHECK(std::find(names.begin(), names.end(), "Sword") != names.end());
    }

    // --- REAL load back into a fresh SceneGraphSystem (== LoadPrefabCommand body). ---
    scene::SceneGraphSystem scene;
    scene::Entity& sceneRoot = scene.GetRoot();
    auto reloadedOpt = serialization::PrefabSerialization::loadPrefab(
        prefabPath.string(), sceneRoot, scene);
    REQUIRE(reloadedOpt.has_value());
    scene::Entity reloaded = *reloadedOpt;
    REQUIRE(reloaded.isAlive());

    // The reloaded root is a FRESH entt id (not in the fake store) -> the registry-fallback handlers
    // serve it, so buildPrefabRigDescFromEntity re-derives the rig the same way the window does after
    // LoadPrefab.
    windows::LiveRigBuildResult postReload = windows::buildPrefabRigDescFromEntity(
        services::internal::toHandle(reloaded.getHandle()));

    // DTO equality: same parts, field-for-field (paths/links/attach) + equal accumulated transforms.
    REQUIRE(postReload.desc.parts.size() == preSave.desc.parts.size());
    for (size_t i = 0; i < preSave.desc.parts.size(); ++i)
    {
        CAPTURE(i);
        CHECK(dtoPartsFieldEqual(postReload.desc.parts[i], preSave.desc.parts[i]));
        CHECK(matEq(postReload.desc.parts[i].localTransform, preSave.desc.parts[i].localTransform));
    }
    // IK round-tripped through the real serializer.
    REQUIRE(postReload.desc.ik.size() == preSave.desc.ik.size());
    CHECK(postReload.desc.ik[0].chain.chainName == preSave.desc.ik[0].chain.chainName);
    CHECK(postReload.desc.ik[0].chain.tipBoneName == preSave.desc.ik[0].chain.tipBoneName);
    CHECK(postReload.desc.ik[0].chain.chainBoneNames == preSave.desc.ik[0].chain.chainBoneNames);
    CHECK(postReload.desc.ik[0].chain.weight == doctest::Approx(preSave.desc.ik[0].chain.weight));
    CHECK(postReload.desc.ik[0].bodyPartIndex == preSave.desc.ik[0].bodyPartIndex);
    CHECK(postReload.desc.ik[0].targetPartIndex == preSave.desc.ik[0].targetPartIndex);

    // The reloaded TREE carries the normalization: root is active and has NO sandbox tag (the tag is
    // a per-window live artifact, never instantiated).
    CHECK(reloaded.getComponent<components::NameComponent>().isActive == true);
    CHECK_FALSE(reloaded.hasComponent<components::PreviewSandboxTagComponent>());

    // Cleanup (registry singleton hygiene + temp file).
    destroyRealSubtree(body);
    destroyRealSubtree(reloaded);
    fs::remove_all(roundtripTestDir(), ec);
    asset::AssetDatabase::instance().clear();
    store().clear();
}

// Negative control: the isActive normalization is GATED on the sandbox tag. An UNtagged inactive
// entity must keep isActive=false through the save (proving the normalization is the tag's doing,
// not a blanket "force active" — a real authored-inactive prefab node must stay inactive).
TEST_CASE("4c: untagged inactive node keeps isActive=false on save (normalization is tag-gated)")
{
    store().clear();
    asset::AssetDatabase::instance().clear();
    std::error_code ec;
    fs::create_directories(roundtripTestDir(), ec);

    // Two children under a (savable, non-root) parent: one tagged+inactive, one untagged+inactive.
    scene::Entity parent = makeRealNode("Parent", makeXf({0, 0, 0}), /*sandboxRoot*/ true); // tagged, inactive
    setRealMesh(parent, "assets/parent.vfMesh", "assets/parent.vfAnimator");

    scene::Entity plain = makeRealNode("PlainInactive", makeXf({1, 0, 0})); // NOT a sandbox root
    plain.getComponent<components::NameComponent>().isActive = false;        // authored inactive
    setRealMesh(plain, "assets/plain.vfMesh");
    parent.addChildren(plain);

    const fs::path prefabPath = roundtripTestDir() / "NormalizationGate.vfPrefab";
    REQUIRE(serialization::PrefabSerialization::savePrefab(parent, prefabPath.string()));

    std::ifstream file(prefabPath);
    REQUIRE(file.is_open());
    json prefabJson;
    file >> prefabJson;
    const json& rootJson = prefabJson["prefab"]["entity"];

    // Tagged root -> normalized active.
    REQUIRE(rootJson.contains("isActive"));
    CHECK(rootJson["isActive"].get<bool>() == true);

    // Find the untagged child node in the saved children and confirm it STAYED inactive.
    REQUIRE(rootJson.contains("children"));
    bool foundPlain = false;
    for (const auto& child : rootJson["children"])
    {
        if (child.value("name", std::string{}) == "PlainInactive")
        {
            foundPlain = true;
            REQUIRE(child.contains("isActive"));
            CHECK(child["isActive"].get<bool>() == false); // untagged -> not normalized
        }
    }
    CHECK(foundPlain);

    destroyRealSubtree(parent);
    fs::remove_all(roundtripTestDir(), ec);
    asset::AssetDatabase::instance().clear();
    store().clear();
}

} // TEST_SUITE("PrefabRigSaveRoundtrip")
