#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <memory>
#include <optional>
#include <map>
#include <string>

namespace scene {
    class SceneGraphSystem;
}

namespace events {
    class EventDispatcher;
}

namespace services {

    class MaterialComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit MaterialComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // Material Operations
        bool addMaterialComponent(EntityHandle entity);
        bool removeMaterialComponent(EntityHandle entity);
        bool hasMaterialComponent(EntityHandle entity) const;
        std::optional<MaterialData> getMaterialData(EntityHandle entity) const;
        bool setMaterialData(EntityHandle entity, const MaterialData& material);
        bool setDefaultMaterial(EntityHandle entity, const std::string& materialPath);
        bool setSubMeshMaterial(EntityHandle entity, const std::string& submeshName, const std::string& materialPath);
        std::string getSubMeshMaterial(EntityHandle entity, const std::string& submeshName) const;
        std::map<std::string, std::string> getAllSubMeshMaterials(EntityHandle entity) const;

        // Runtime named-parameter overrides (typed)
        bool setMaterialParameter(EntityHandle entity, const std::string& parameterName,
                                  const ::material::ParameterValue& value);
        bool clearMaterialParameter(EntityHandle entity, const std::string& parameterName);
        std::optional<::material::ParameterValue> getMaterialParameter(
            EntityHandle entity, const std::string& parameterName) const;
    };

}
