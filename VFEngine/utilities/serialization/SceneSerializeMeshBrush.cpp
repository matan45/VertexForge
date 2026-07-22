#include "SceneSerialization.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    // VK-1570: persist mesh-brush instance tracking metadata. Mesh/material/transform/collider
    // already persist via normal entity serialization; this component restores only the brush's
    // per-instance bookkeeping (palette type + placement normal + provenance group id) so the
    // brush can erase and space-check reloaded instances. Rides along the entity's UUID identity.

    json SceneSerialization::serializeMeshBrushInstance(
        const components::MeshBrushInstanceComponent& brush)
    {
        json j;
        j["brushGroupId"] = brush.brushGroupId;
        j["paletteIndex"] = brush.paletteIndex;
        j["surfaceNormal"] = json::array({brush.surfaceNormal.x, brush.surfaceNormal.y, brush.surfaceNormal.z});
        return j;
    }

    void SceneSerialization::deserializeMeshBrushInstance(
        const json& j, components::MeshBrushInstanceComponent& brush)
    {
        // Tolerant reads: missing/malformed fields keep the struct defaults (brushGroupId=0,
        // paletteIndex=0, surfaceNormal={0,1,0}) so hand-edited or older scenes still load.
        if (auto it = j.find("brushGroupId"); it != j.end() && it->is_number_unsigned())
            brush.brushGroupId = it->get<uint32_t>();
        if (auto it = j.find("paletteIndex"); it != j.end() && it->is_number_unsigned())
            brush.paletteIndex = it->get<uint32_t>();
        if (auto it = j.find("surfaceNormal"); it != j.end() && it->is_array() && it->size() >= 3)
            brush.surfaceNormal = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    }
}
