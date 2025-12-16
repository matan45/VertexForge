#pragma once
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace services {

  
    class IOffScreenProvider {
    public:
        virtual ~IOffScreenProvider() = default;

        // Lifecycle
        virtual void init() = 0;
        virtual void cleanUp() = 0;
        
        virtual void* render() = 0;
        
        virtual void iblSet(std::string_view iblPath) = 0;
        virtual void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection) = 0;
        virtual void iblRemove() = 0;
        
        virtual std::string meshLoad(std::string_view meshPath) = 0;
        virtual void meshUnload(const std::string& meshId) = 0;
        virtual void meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos, float time = 0.0f) = 0;
        virtual bool isMeshLoaded(const std::string& meshPath) const = 0;
        virtual std::vector<std::string> getLoadedMeshes() const = 0;
        virtual void prepareFrameMeshes() = 0;
    };

}
