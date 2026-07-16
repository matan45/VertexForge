#pragma once

#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace material
{
    // Collapse a raw list of material asset paths into a deterministic, duplicate-free
    // set: drops empty strings, sorts, and removes adjacent duplicates. Pure (no I/O),
    // so it is directly unit-testable.
    std::vector<std::string> dedupeWarmupPaths(std::vector<std::string> raw);

    // Walk every MaterialComponent in the registry and collect the resolved asset paths
    // of its default material plus each per-submesh material, returning the deduped union.
    // Used at scene load (VK-1532) to know which material pipelines to warm ahead of the
    // first draw. Only valid AssetRefs are resolved.
    std::vector<std::string> collectMaterialPathsForWarmup(const entt::registry& registry);
}
