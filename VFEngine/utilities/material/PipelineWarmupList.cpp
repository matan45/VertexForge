#include "PipelineWarmupList.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "../components/CoreComponents.hpp"
#include "../asset/AssetRef.hpp"

namespace material
{
    std::vector<std::string> dedupeWarmupPaths(std::vector<std::string> raw)
    {
        std::erase_if(raw, [](const std::string& p) { return p.empty(); });
        std::sort(raw.begin(), raw.end());
        raw.erase(std::unique(raw.begin(), raw.end()), raw.end());
        return raw;
    }

    std::vector<std::string> collectMaterialPathsForWarmup(const entt::registry& registry)
    {
        std::vector<std::string> paths;

        // AssetRef::resolve() caches into the ref's mutable members. The render thread resolves
        // these same component refs during frame extraction, so resolve a *copy* here to avoid a
        // data race on the shared component's cached path.
        auto resolveCopy = [](const asset::AssetRef& ref) -> std::string
        {
            asset::AssetRef copy = ref;
            return copy.resolve();
        };

        auto view = registry.view<const components::MaterialComponent>();
        for (auto entity : view)
        {
            const auto& mat = view.get<const components::MaterialComponent>(entity);

            if (mat.defaultMaterialRef.isValid())
            {
                paths.push_back(resolveCopy(mat.defaultMaterialRef));
            }
            for (const auto& [submeshName, ref] : mat.subMeshMaterials)
            {
                if (ref.isValid())
                {
                    paths.push_back(resolveCopy(ref));
                }
            }
        }

        return dedupeWarmupPaths(std::move(paths));
    }
}
