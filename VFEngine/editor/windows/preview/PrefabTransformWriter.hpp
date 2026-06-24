#pragma once

// VK-1433 (Save Transforms to Prefab) — header-only, entt-free writer that bakes the rig
// preview's editor-transient gizmo transforms back into a .vfPrefab JSON's per-entity
// `transform` blocks.
//
// Kept header-only (like PrefabRigDescBuilder.hpp) so the Tests project can exercise the
// compose/decompose + node-mapping logic CPU-only, with no imgui / no Graphics / no entt and
// WITHOUT the entt-based PrefabSerialization (preserving the rig preview's isolation invariant).
//
// Mapping: buildPrefabRigDescDTO (PrefabRigDescBuilder.hpp) assigns part indices in DFS pre-order
// over the prefab tree, counting only MESH-BEARING nodes. This writer walks the SAME raw JSON
// tree in the SAME order, so the k-th mesh-bearing node is part k — the identity the gizmo edits.
//
// Round-trip safety: the caller loads the prefab JSON, passes it here, and dumps it back. This
// function mutates ONLY the targeted nodes' `transform` (position/rotation/scale) — every other
// field (components, children, isActive, isStatic, version, prefab name) is left byte-for-byte
// untouched, so unrelated data cannot be corrupted.
//
// Semantics + a known limitation: the gizmo's previewTransform is composed onto the node's stored
// LOCAL transform (newLocal = oldLocal * preview) and re-decomposed to the TRS schema. This is
// exact for translation, rotation, and UNIFORM scale. If a part already has NON-uniform scale AND
// the gizmo rotates it, the product matrix contains shear that a translate*R-xyz*diagonal-scale
// TRS cannot represent — the decompose drops the shear (position stays exact; orientation/scale
// approximate). The gizmo also lives in the assembly's attachment frame rather than the node's
// parent-entity frame; baking it into the entity-local transform is the agreed, schema-faithful
// mapping (the schema only stores a local TRS).

#include "math/TransformUtils.hpp" // math::compose/decomposeMatrix (XYZ euler deg == TransformComponent)

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace windows::prefabtransform
{
    using json = nlohmann::json;

    // Mirror PrefabPreviewWindow.cpp::readRefPath for the mesh ref: writeAssetRef stores the GUID
    // in "<key>" and the resolved disk path in "<key>Path". A node is "mesh-bearing" iff this
    // returns a non-empty path (same rule as PrefabEntityNode::hasMesh()).
    inline std::string readMeshRefPath(const json& components)
    {
        auto mit = components.find("mesh");
        if (mit == components.end() || !mit->is_object())
            return {};
        const json& mesh = *mit;

        if (auto it = mesh.find("meshRefPath"); it != mesh.end() && it->is_string())
            return it->get<std::string>();

        if (auto it = mesh.find("meshRef"); it != mesh.end() && it->is_string())
        {
            std::string val = it->get<std::string>();
            const bool looksLikePath = val.find('.') != std::string::npos ||
                                       val.find('/') != std::string::npos ||
                                       val.find('\\') != std::string::npos;
            if (looksLikePath) return val;
        }
        return {};
    }

    inline bool nodeHasMesh(const json& entityNode)
    {
        auto cit = entityNode.find("components");
        if (cit == entityNode.end() || !cit->is_object())
            return false;
        return !readMeshRefPath(*cit).empty();
    }

    // Read an entity node's current LOCAL transform (position/rotation-euler-deg/scale). Missing or
    // malformed fields default to identity (position 0, rotation 0, scale 1) — matching the read
    // path's parse defaults so an absent `transform` round-trips as identity.
    inline math::DecomposedTransform readLocalTransform(const json& entityNode)
    {
        math::DecomposedTransform t; // identity defaults
        auto it = entityNode.find("transform");
        if (it == entityNode.end() || !it->is_object())
            return t;
        const json& tj = *it;

        auto readVec3 = [](const json& arr, glm::vec3 fallback) -> glm::vec3
        {
            if (arr.is_array() && arr.size() >= 3)
                return glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
            return fallback;
        };

        if (auto p = tj.find("position"); p != tj.end()) t.position = readVec3(*p, t.position);
        if (auto r = tj.find("rotation"); r != tj.end()) t.rotation = readVec3(*r, t.rotation);
        if (auto s = tj.find("scale"); s != tj.end())    t.scale    = readVec3(*s, t.scale);
        return t;
    }

    // Write position/rotation/scale into a node's `transform`, preserving any other transform keys
    // (e.g. isStatic). Uses the [x,y,z] array shape SceneSerialization::serializeTransform writes.
    inline void writeLocalTransform(json& entityNode, const math::DecomposedTransform& t)
    {
        json& tj = entityNode["transform"]; // create the object if absent (preserves siblings)
        if (!tj.is_object())
            tj = json::object();
        tj["position"] = json::array({t.position.x, t.position.y, t.position.z});
        tj["rotation"] = json::array({t.rotation.x, t.rotation.y, t.rotation.z});
        tj["scale"]    = json::array({t.scale.x, t.scale.y, t.scale.z});
    }

    namespace detail
    {
        // DFS pre-order over the prefab entity tree, collecting mutable pointers to each node so we
        // can rewrite a node's transform in place. Order MUST match PrefabRigDescBuilder's flatten.
        inline void flatten(json& node, std::vector<json*>& out)
        {
            out.push_back(&node);
            if (auto it = node.find("children"); it != node.end() && it->is_array())
            {
                for (json& child : *it)
                {
                    if (child.is_object())
                        flatten(child, out);
                }
            }
        }
    }

    // Bake gizmo previewTransforms into the prefab JSON's entity transforms.
    //   prefabJson         : the loaded .vfPrefab root (expects prefabJson["prefab"]["entity"]).
    //   previewTransforms  : part index -> editor-transient gizmo delta (assembly's partWorld =
    //                        base * previewTransform). Identity entries are skipped (no-op edit).
    //   skipParts          : part indices to NOT write (e.g. the ROOT part, whose gizmo = whole-rig
    //                        framing rather than a local node transform).
    // For each applicable part: newLocal = oldLocal * previewTransform, decomposed to the schema's
    // TRS and written into that node's `transform`. Returns the number of nodes actually rewritten.
    inline int applyPreviewTransforms(json& prefabJson,
                                      const std::map<int, glm::mat4>& previewTransforms,
                                      const std::set<int>& skipParts)
    {
        auto pit = prefabJson.find("prefab");
        if (pit == prefabJson.end() || !pit->is_object())
            return 0;
        auto eit = pit->find("entity");
        if (eit == pit->end() || !eit->is_object())
            return 0;

        std::vector<json*> nodes;
        detail::flatten(*eit, nodes);

        int written = 0;
        int partIndex = 0; // increments only on mesh-bearing nodes (matches buildPrefabRigDescDTO)
        for (json* nodePtr : nodes)
        {
            json& node = *nodePtr;
            if (!nodeHasMesh(node))
                continue;

            const int thisPart = partIndex++;

            if (skipParts.count(thisPart))
                continue;

            auto tIt = previewTransforms.find(thisPart);
            if (tIt == previewTransforms.end())
                continue;
            if (tIt->second == glm::mat4(1.0f))
                continue; // user never moved this part

            const math::DecomposedTransform old = readLocalTransform(node);
            const glm::mat4 oldLocal = math::composeMatrix(old.position, old.rotation, old.scale);
            const glm::mat4 newLocal = oldLocal * tIt->second;

            writeLocalTransform(node, math::decomposeMatrix(newLocal));
            ++written;
        }

        return written;
    }

    // Phase 2 — zero the LOCAL translation of the node that became `part` (k-th mesh-bearing node in
    // DFS pre-order, the same mapping as above). Used by the socket-attached-child TRANSLATE-drop
    // "Zero translation" action: a socketed child's position is dropped at instantiation, so baking
    // it to zero makes the prefab match runtime. Rotation/scale and every other field are preserved.
    // Returns true iff the node was found and rewritten.
    inline bool zeroPartTranslation(json& prefabJson, int part)
    {
        if (part < 0) return false;

        auto pit = prefabJson.find("prefab");
        if (pit == prefabJson.end() || !pit->is_object())
            return false;
        auto eit = pit->find("entity");
        if (eit == pit->end() || !eit->is_object())
            return false;

        std::vector<json*> nodes;
        detail::flatten(*eit, nodes);

        int partIndex = 0;
        for (json* nodePtr : nodes)
        {
            json& node = *nodePtr;
            if (!nodeHasMesh(node))
                continue;

            if (partIndex++ != part)
                continue;

            math::DecomposedTransform t = readLocalTransform(node);
            t.position = glm::vec3(0.0f);
            writeLocalTransform(node, t);
            return true;
        }
        return false;
    }
}
