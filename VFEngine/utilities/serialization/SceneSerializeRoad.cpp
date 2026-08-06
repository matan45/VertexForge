#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    // VK-1621: persist a generated road's authoring data. The mesh, material, transform and
    // collider of each chunk already persist via normal entity serialization; this restores the
    // SOURCE spline and profile so the road can be re-opened in the Spline tool, edited and
    // regenerated after a reload. Rides the entity's UUID identity, same as
    // MeshBrushInstanceComponent (VK-1570).

    json SceneSerialization::serializeRoadSpline(const components::RoadSplineComponent& road)
    {
        json j;

        json points = json::array();
        for (const glm::vec3& p : road.controlPoints)
            points.push_back(json::array({p.x, p.y, p.z}));
        j["controlPoints"] = std::move(points);

        const terrain::SplineParams& params = road.params;
        j["ops"] = static_cast<uint8_t>(params.ops);
        j["corridorWidth"] = params.corridorWidth;
        j["falloffWidth"] = params.falloffWidth;
        j["embankmentHeight"] = params.embankmentHeight;
        j["paintLayer"] = params.paintLayer;
        j["roadName"] = params.roadName;
        j["roadMaterialPath"] = params.roadMaterialPath;
        j["roadCollider"] = params.roadCollider;

        const terrain::RoadProfile& profile = params.road;
        json columns = json::array();
        for (const terrain::RoadProfileColumn& column : profile.columns)
        {
            columns.push_back(json{{"offset", column.offset},
                                   {"u", column.u},
                                   {"heightOffset", column.heightOffset},
                                   {"terrainBlend", column.terrainBlend}});
        }
        j["profile"] = json{{"columns", std::move(columns)},
                            {"ringSpacing", profile.ringSpacing},
                            {"uvTilingU", profile.uvTilingU},
                            {"uvTilingV", profile.uvTilingV},
                            {"zOffset", profile.zOffset},
                            {"flatCrossSection", profile.flatCrossSection},
                            {"chunkMinTileFraction", profile.chunkMinTileFraction}};

        writeAssetRef(j, "generatedMesh", road.generatedMeshRef);
        j["splineId"] = road.splineId;
        j["revision"] = road.revision;
        j["chunkCount"] = road.chunkCount;

        return j;
    }

    void SceneSerialization::deserializeRoadSpline(const json& j, components::RoadSplineComponent& road)
    {
        // Tolerant reads throughout: a missing or malformed field keeps the struct default, so a
        // hand-edited or older scene still loads rather than failing (SceneSerializeMeshBrush.cpp:24).
        auto readFloat = [&j](const char* key, float fallback) -> float
        {
            const auto it = j.find(key);
            return (it != j.end() && it->is_number()) ? it->get<float>() : fallback;
        };

        road.controlPoints.clear();
        if (auto it = j.find("controlPoints"); it != j.end() && it->is_array())
        {
            road.controlPoints.reserve(it->size());
            for (const json& p : *it)
            {
                // The members are checked, not just the array shape. get<float>() on a string,
                // bool, null or object throws json::type_error, and deserializeRoadSpline's caller
                // (SceneSerializeDispatchDeserialize.cpp) has no try/catch — so one `null` from a
                // hand edit or an interrupted save would fail the WHOLE scene load, while every
                // other malformed road field here quietly falls back to its default.
                if (p.is_array() && p.size() >= 3 &&
                    p[0].is_number() && p[1].is_number() && p[2].is_number())
                    road.controlPoints.emplace_back(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            }
        }

        terrain::SplineParams& params = road.params;
        if (auto it = j.find("ops"); it != j.end() && it->is_number_unsigned())
            params.ops = static_cast<terrain::SplineOps>(it->get<uint8_t>());
        params.corridorWidth = readFloat("corridorWidth", params.corridorWidth);
        params.falloffWidth = readFloat("falloffWidth", params.falloffWidth);
        params.embankmentHeight = readFloat("embankmentHeight", params.embankmentHeight);
        if (auto it = j.find("paintLayer"); it != j.end() && it->is_number_unsigned())
            params.paintLayer = it->get<uint32_t>();
        if (auto it = j.find("roadName"); it != j.end() && it->is_string())
            params.roadName = it->get<std::string>();
        if (auto it = j.find("roadMaterialPath"); it != j.end() && it->is_string())
            params.roadMaterialPath = it->get<std::string>();
        if (auto it = j.find("roadCollider"); it != j.end() && it->is_boolean())
            params.roadCollider = it->get<bool>();

        if (auto profileIt = j.find("profile"); profileIt != j.end() && profileIt->is_object())
        {
            const json& p = *profileIt;
            terrain::RoadProfile& profile = params.road;

            auto readProfileFloat = [&p](const char* key, float fallback) -> float
            {
                const auto it = p.find(key);
                return (it != p.end() && it->is_number()) ? it->get<float>() : fallback;
            };

            // Only replace the default columns when the stored list is usable — a profile with
            // fewer than two columns cannot produce geometry, and silently keeping it would make
            // the road regenerate to nothing.
            if (auto colIt = p.find("columns"); colIt != p.end() && colIt->is_array() && colIt->size() >= 2)
            {
                std::vector<terrain::RoadProfileColumn> columns;
                columns.reserve(colIt->size());
                for (const json& c : *colIt)
                {
                    if (!c.is_object())
                        continue;
                    terrain::RoadProfileColumn column;
                    if (auto it = c.find("offset"); it != c.end() && it->is_number())
                        column.offset = it->get<float>();
                    if (auto it = c.find("u"); it != c.end() && it->is_number())
                        column.u = it->get<float>();
                    if (auto it = c.find("heightOffset"); it != c.end() && it->is_number())
                        column.heightOffset = it->get<float>();
                    if (auto it = c.find("terrainBlend"); it != c.end() && it->is_number())
                        column.terrainBlend = it->get<float>();
                    columns.push_back(column);
                }
                if (columns.size() >= 2)
                    profile.columns = std::move(columns);
            }

            profile.ringSpacing = readProfileFloat("ringSpacing", profile.ringSpacing);
            profile.uvTilingU = readProfileFloat("uvTilingU", profile.uvTilingU);
            profile.uvTilingV = readProfileFloat("uvTilingV", profile.uvTilingV);
            profile.zOffset = readProfileFloat("zOffset", profile.zOffset);
            profile.chunkMinTileFraction =
                readProfileFloat("chunkMinTileFraction", profile.chunkMinTileFraction);
            if (auto it = p.find("flatCrossSection"); it != p.end() && it->is_boolean())
                profile.flatCrossSection = it->get<bool>();
        }

        road.generatedMeshRef = readAssetRef(j, "generatedMesh");
        if (auto it = j.find("splineId"); it != j.end() && it->is_number_unsigned())
            road.splineId = it->get<uint64_t>();
        if (auto it = j.find("revision"); it != j.end() && it->is_number_unsigned())
            road.revision = it->get<uint32_t>();
        if (auto it = j.find("chunkCount"); it != j.end() && it->is_number_unsigned())
            road.chunkCount = it->get<uint32_t>();
    }
}
