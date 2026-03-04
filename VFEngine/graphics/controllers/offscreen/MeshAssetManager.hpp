#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include "../../../services/providers/render/IOffScreenProvider.hpp"

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class MeshAssetManager
    {
    private:
        render::RenderPassHandler& renderHandler;
    public:
        explicit MeshAssetManager(render::RenderPassHandler& renderHandler);

        std::string load(std::string_view meshPath);
        void unload(const std::string& meshId);
        bool isLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;
        std::optional<services::MeshBounds> getBoundingBox(const std::string& meshPath) const;
    };
}
