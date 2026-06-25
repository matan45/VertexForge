#pragma once

// Phase 2 — header-only, CPU-testable validation helpers for the Prefab Rig Preview window.
//
// Kept header-only (like PrefabRigDescBuilder.hpp / PrefabTransformWriter.hpp) so the Tests project
// can exercise the logic with no imgui / no Graphics / no filesystem. The window injects the real
// std::filesystem::exists predicate; tests inject a fake map so coverage is deterministic.
//
//   * validatePartRefs(desc, existsFn) -> per-part broken-asset-reference report. A reference is
//     "missing" when its path is non-empty AND existsFn(path) == false. (An EMPTY path is "no
//     reference", not a broken one — e.g. a static part legitimately has no animator/retarget.)
//   * childHasDroppedTranslation(parentPartIndex, position) -> the verified socket-attached-child
//     TRANSLATE-drop case: SocketAttachmentUpdater::applyModelOffset builds entityLocal = rot*scale
//     and DROPS translation, so a non-zero source position on a socketed child will NOT reproduce at
//     instantiation. The window warns (and offers a one-click zero) instead of silently losing it.

#include "data/PrefabRigDescDTO.hpp"
#include "PrefabRigDescBuilder.hpp" // PrefabEntityNode + detail::flattenNodes (DFS pre-order)

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace windows::prefabrigval
{
    // Which references on one part are missing on disk (paths that are present but do not resolve).
    struct PartRefStatus
    {
        bool meshMissing = false;
        bool animatorMissing = false;
        bool retargetMissing = false;
        bool defaultMaterialMissing = false;
        std::vector<std::string> missingSubMeshMaterials; // submesh names whose material is missing

        // Any broken reference at all (drives the part's red badge).
        bool anyMissing() const
        {
            return meshMissing || animatorMissing || retargetMissing ||
                   defaultMaterialMissing || !missingSubMeshMaterials.empty();
        }
    };

    using ExistsPredicate = std::function<bool(const std::string&)>;

    // Validate one part's references. A path is checked only when non-empty.
    inline PartRefStatus validatePartRef(const services::PrefabRigPartDTO& part, const ExistsPredicate& exists)
    {
        PartRefStatus status;
        auto broken = [&](const std::string& p) { return !p.empty() && !exists(p); };

        status.meshMissing = broken(part.meshPath);
        status.animatorMissing = broken(part.animatorPath);
        status.retargetMissing = broken(part.retargetPath);
        status.defaultMaterialMissing = broken(part.defaultMaterialPath);
        for (const auto& [submesh, matPath] : part.subMeshMaterials)
        {
            if (broken(matPath))
                status.missingSubMeshMaterials.push_back(submesh);
        }
        return status;
    }

    // Per-part broken-ref report for a whole rig description (parallel to desc.parts).
    inline std::vector<PartRefStatus> validatePartRefs(const services::PrefabRigDescDTO& desc,
                                                       const ExistsPredicate& exists)
    {
        std::vector<PartRefStatus> out;
        out.reserve(desc.parts.size());
        for (const auto& part : desc.parts)
            out.push_back(validatePartRef(part, exists));
        return out;
    }

    // Total number of parts that have at least one broken reference (the info-panel "N missing refs").
    inline int countPartsWithMissingRefs(const std::vector<PartRefStatus>& statuses)
    {
        int n = 0;
        for (const auto& s : statuses)
            if (s.anyMissing()) ++n;
        return n;
    }

    // The socket-attached-child TRANSLATE-drop case. True when the part is socket-attached
    // (parentPartIndex >= 0) AND its source local position is non-zero — that translation is
    // dropped at instantiation, so the window warns. epsilon guards float noise.
    inline bool childHasDroppedTranslation(int parentPartIndex, const glm::vec3& sourcePosition,
                                           float epsilon = 1e-5f)
    {
        if (parentPartIndex < 0) return false; // a root part keeps its translation
        return std::fabs(sourcePosition.x) > epsilon ||
               std::fabs(sourcePosition.y) > epsilon ||
               std::fabs(sourcePosition.z) > epsilon;
    }

    // The source LOCAL position of the node that became `part`. buildPrefabRigDescDTO assigns part
    // indices in DFS pre-order over MESH-BEARING nodes, so the k-th mesh-bearing node is part k —
    // the same mapping the transform writer uses. Returns {0,0,0} for an out-of-range index. This is
    // the node's own position (what the user would zero), NOT the accumulated world transform.
    inline glm::vec3 sourcePositionForPart(const PrefabEntityNode& root, int part)
    {
        if (part < 0) return glm::vec3(0.0f);
        std::vector<const PrefabEntityNode*> nodes;
        detail::flattenNodes(root, nodes);
        int k = 0;
        for (const PrefabEntityNode* n : nodes)
        {
            if (!n->hasMesh()) continue;
            if (k == part) return n->position;
            ++k;
        }
        return glm::vec3(0.0f);
    }
}
