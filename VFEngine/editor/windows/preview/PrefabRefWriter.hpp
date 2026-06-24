#pragma once

// Phase 2 — header-only, entt-free writer that persists a drag-drop part-ref swap back into a
// .vfPrefab JSON. Mirrors PrefabTransformWriter: load -> mutate ONLY the targeted ref keys -> dump,
// leaving every other field byte-for-byte untouched.
//
// SCHEMA (verified against serialization::writeAssetRef): an AssetRef is stored as TWO keys —
//   "<key>"      = the asset's hex GUID  (ref.toHexString())
//   "<key>Path"  = the resolved disk path (ref.resolve()), a load-time fallback for a cold DB.
// MeshComponent uses meshRef[Path] / animatorRef[Path]; MaterialComponent uses defaultMaterialRef[Path].
//
// GUID resolution (the schema-sensitive part): resolving a path -> GUID requires the runtime
// AssetDatabase (asset::AssetRef::fromPath(path).toHexString()), which is NOT header-only-available
// and NOT linkable in the Tests project. So the GUID is supplied via an injected `guidResolver`
// callback: the editor passes a real resolver, tests pass a fake (or none).
//
// When the resolver yields a non-empty GUID we write `<key>` = GUID + `<key>Path` = path (the
// canonical writeAssetRef shape). When it yields EMPTY (cold DB / fake), we must NOT (a) keep the OLD
// GUID — it would mis-point the loaded prefab at the previous asset — and must NOT (b) erase the key
// or write a zero/invalid-GUID hex: readAssetRef (AssetRefSerializationHelper.hpp ~:29-94) only
// consults `<key>Path` when `<key>` holds a VALID-but-unresolvable GUID; an absent key or an invalid
// (value 0) GUID returns AssetRef::invalid() WITHOUT ever reading the path. The reader's REAL path
// recovery is its legacy "looks like a path" branch (val contains '.'/'/'/'\\' -> AssetRef::fromPath,
// which auto-registers). So the empty-resolver fallback writes the NEW path into BOTH `<key>` and
// `<key>Path`: the reader detects `<key>` is path-shaped and resolves the NEW asset via fromPath().

#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

namespace windows::prefabref
{
    using json = nlohmann::json;

    // path -> hex GUID, or empty if unresolvable (cold DB / fake). Injected so this stays testable.
    using GuidResolver = std::function<std::string(const std::string&)>;

    // Mirror PrefabTransformWriter::readMeshRefPath / nodeHasMesh so the part-index mapping (k-th
    // mesh-bearing node in DFS pre-order == part k) is identical.
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

    namespace detail
    {
        // DFS pre-order over the prefab entity tree (order matches PrefabRigDescBuilder's flatten).
        inline void flatten(json& node, std::vector<json*>& out)
        {
            out.push_back(&node);
            if (auto it = node.find("children"); it != node.end() && it->is_array())
            {
                for (json& child : *it)
                    if (child.is_object()) flatten(child, out);
            }
        }
    }

    // Write an AssetRef pair into `obj`, using `resolveGuid` to map the path -> GUID. The resolved
    // GUID goes in `<key>` and the path in `<key>Path`. When the resolver yields EMPTY, we write the
    // NEW path into BOTH keys (NOT the stale GUID, NOT an erase) so readAssetRef's legacy path-shaped
    // branch resolves the new asset — see the file header for why erase/zero-GUID would dead-ref.
    // Returns true (always writes the path).
    inline bool writeRefPair(json& obj, const std::string& key, const std::string& path,
                             const GuidResolver& resolveGuid)
    {
        const std::string pathKey = key + "Path";
        obj[pathKey] = path;

        std::string guid = resolveGuid ? resolveGuid(path) : std::string();
        // GUID resolved -> canonical GUID key. Empty -> path-shaped value so the reader's fromPath
        // branch fires (the path always "looks like a path": it carries the .vf* extension's dot).
        obj[key] = !guid.empty() ? guid : path;
        return true;
    }

    // The ref keys one part-swap can change (extension-derived by the caller).
    struct PartRefEdit
    {
        int part = -1;                 // mesh-bearing part index (DFS pre-order)
        std::string meshPath;          // non-empty => rewrite mesh.meshRef[Path]
        std::string animatorPath;      // non-empty => rewrite mesh.animatorRef[Path]
        std::string defaultMaterialPath; // non-empty => rewrite material.defaultMaterialRef[Path]
    };

    // Apply ref edits to the prefab JSON in place. For each edit, find the k-th mesh-bearing node and
    // rewrite only the requested ref pairs. Creates the `mesh` / `material` component objects only if
    // a corresponding edit is present (an animator/material swap on a node that lacked the component
    // adds it — matching how the live swap re-points the part). Returns the number of edits applied.
    inline int applyRefEdits(json& prefabJson, const std::vector<PartRefEdit>& edits,
                             const GuidResolver& resolveGuid)
    {
        auto pit = prefabJson.find("prefab");
        if (pit == prefabJson.end() || !pit->is_object())
            return 0;
        auto eit = pit->find("entity");
        if (eit == pit->end() || !eit->is_object())
            return 0;

        std::vector<json*> nodes;
        detail::flatten(*eit, nodes);

        int applied = 0;
        for (const PartRefEdit& edit : edits)
        {
            if (edit.part < 0) continue;

            int partIndex = 0;
            json* target = nullptr;
            for (json* nodePtr : nodes)
            {
                if (!nodeHasMesh(*nodePtr)) continue;
                if (partIndex++ == edit.part) { target = nodePtr; break; }
            }
            if (!target) continue;

            json& components = (*target)["components"];
            if (!components.is_object()) components = json::object();

            bool any = false;
            if (!edit.meshPath.empty() || !edit.animatorPath.empty())
            {
                json& mesh = components["mesh"];
                if (!mesh.is_object()) mesh = json::object();
                if (!edit.meshPath.empty())     { writeRefPair(mesh, "meshRef", edit.meshPath, resolveGuid); any = true; }
                if (!edit.animatorPath.empty()) { writeRefPair(mesh, "animatorRef", edit.animatorPath, resolveGuid); any = true; }
            }
            if (!edit.defaultMaterialPath.empty())
            {
                json& mat = components["material"];
                if (!mat.is_object()) mat = json::object();
                writeRefPair(mat, "defaultMaterialRef", edit.defaultMaterialPath, resolveGuid);
                any = true;
            }
            if (any) ++applied;
        }
        return applied;
    }
}
