#include "TerrainToolSupport.hpp"
#include "PathSandbox.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/terrain/TerrainMaterialAssetEvents.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "string/FileNameSanitize.hpp"
#include "terrain/TerrainTypes.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <system_error>

namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace authoring = events::terrainAuthoring;

        // Guards uniqueFilePath against a folder full of numbered copies.
        constexpr int maxUniqueSuffix = 9999;

        nlohmann::json idJson(const services::EntityHandle& handle)
        {
            return handle.isValid() ? nlohmann::json(static_cast<uint32_t>(handle.id)) : nlohmann::json(nullptr);
        }

        std::string idText(const services::EntityHandle& handle)
        {
            return std::to_string(static_cast<uint32_t>(handle.id));
        }

        std::string formatNumber(double value)
        {
            return std::format("{}", value);
        }

        std::string describeAll(const std::vector<const services::TerrainSummary*>& terrains)
        {
            std::string out;
            for (const services::TerrainSummary* terrain : terrains)
            {
                out += out.empty() ? describeTerrain(*terrain) : ", " + describeTerrain(*terrain);
            }
            return out;
        }

        std::string shellError(const services::TerrainSummary& terrain)
        {
            return "Terrain " + describeTerrain(terrain) + " has no data loaded: it is an empty shell (its .vfTerrain "
                   "is missing or failed to load). Remove it with terrain_delete and create a new terrain with "
                   "terrain_create";
        }

        const char* strokeStatusName(services::TerrainStrokeStatus status)
        {
            using Status = services::TerrainStrokeStatus;
            switch (status)
            {
            case Status::Ok: return "Ok";
            case Status::InvalidArguments: return "InvalidArguments";
            case Status::NoTerrain: return "NoTerrain";
            case Status::SaveInProgress: return "SaveInProgress";
            case Status::GpuUnavailable: return "GpuUnavailable";
            case Status::TooMuchWork: return "TooMuchWork";
            case Status::OffTerrain: return "OffTerrain";
            case Status::NoHeightAtPoint: return "NoHeightAtPoint";
            }
            return "Unknown";
        }

        nlohmann::json terrainMaterialJson(const services::TerrainData& data, const std::optional<fs::path>& root)
        {
            if (data.terrainMaterialPath.empty())
            {
                return nullptr;
            }
            const fs::path file = terrainEnginePath(root, data.terrainMaterialPath);
            nlohmann::json material{{"path", projectRelativeUtf8(root, file)}};

            std::optional<services::TerrainMaterialInfo> info;
            try
            {
                events::terrainMaterial::GetTerrainMaterialInfoQuery query;
                query.materialPath = file.string();
                info = events::EventDispatcher::instance().query(query);
            }
            catch (const std::runtime_error&)
            {
                // No terrain material service (or an unrepresentable path): report what is known.
                material["layers"] = nullptr;
                return material;
            }
            if (!info.has_value())
            {
                material["error"] = "the terrain material could not be loaded";
                return material;
            }

            nlohmann::json layers = nlohmann::json::array();
            for (std::size_t i = 0; i < info->layers.size(); ++i)
            {
                layers.push_back(terrainLayerJson(info->layers[i], static_cast<uint32_t>(i), root));
            }
            material["name"] = info->name;
            material["maxLayers"] = info->maxLayers;
            material["layers"] = std::move(layers);
            return material;
        }

        // A camera_set pose that frames the whole grid from the +Z side, looking down at ~37 degrees.
        nlohmann::json suggestedViewJson(const services::TerrainData& data)
        {
            const TerrainWorldBounds bounds = terrainWorldBounds(data);
            const glm::vec2 centre = (bounds.lower + bounds.upper) * 0.5f;
            const float span = std::max(bounds.upper.x - bounds.lower.x, bounds.upper.y - bounds.lower.y);
            float lookY = 0.0f;
            if (lookY < data.minHeight)
            {
                lookY = data.minHeight;
            }
            if (lookY > data.maxHeight)
            {
                lookY = data.maxHeight;
            }
            return {
                {"position", nlohmann::json::array({centre.x, lookY + 0.6f * span, centre.y + 0.8f * span})},
                {"lookAt", nlohmann::json::array({centre.x, lookY, centre.y})}
            };
        }
    }

    // ------------------------------------------------------------------
    // Project paths
    // ------------------------------------------------------------------

    fs::path terrainProjectRoot()
    {
        auto project = events::EventDispatcher::instance().query(events::project::GetCurrentProjectQuery{});
        if (!project.has_value() || project->workingDirectory.empty())
        {
            throw std::runtime_error("No project is loaded; open one with project_open first");
        }
        // workingDirectory is built with path::string() (narrow) by ProjectServiceImpl.
        return fs::absolute(fs::path(project->workingDirectory));
    }

    std::optional<fs::path> tryTerrainProjectRoot()
    {
        try
        {
            return terrainProjectRoot();
        }
        catch (const std::runtime_error&)
        {
            return std::nullopt;
        }
    }

    fs::path terrainEnginePath(const std::optional<fs::path>& root, const std::string& enginePath)
    {
        if (enginePath.empty())
        {
            return {};
        }
        fs::path path(enginePath);
        if (path.is_relative() && root.has_value())
        {
            path = *root / path;
        }
        return path.lexically_normal();
    }

    std::string projectRelativeUtf8(const std::optional<fs::path>& root, const fs::path& path)
    {
        if (root.has_value())
        {
            std::error_code ec;
            const fs::path relative = fs::relative(path, *root, ec);
            if (!ec && !relative.empty() && *relative.begin() != "..")
            {
                return genericPathToUtf8(relative);
            }
        }
        return genericPathToUtf8(path);
    }

    bool sameTerrainFile(const fs::path& a, const fs::path& b)
    {
        if (a.empty() || b.empty())
        {
            return false;
        }
        std::error_code aEc;
        std::error_code bEc;
        if (fs::exists(a, aEc) && fs::exists(b, bEc))
        {
            std::error_code ec;
            const bool same = fs::equivalent(a, b, ec);
            if (!ec)
            {
                return same;
            }
        }
        return detail::componentEquals(a.lexically_normal(), b.lexically_normal());
    }

    fs::path uniqueFilePath(const fs::path& directory, const std::string& stem, std::string_view extension,
                            const std::vector<fs::path>& claimed)
    {
        auto taken = [&claimed](const fs::path& candidate)
        {
            std::error_code ec;
            if (fs::exists(candidate, ec))
            {
                return true;
            }
            return std::any_of(claimed.begin(), claimed.end(),
                               [&candidate](const fs::path& other) { return sameTerrainFile(candidate, other); });
        };

        const std::string suffix(extension);
        fs::path candidate = directory / pathFromUtf8(stem + suffix);
        for (int n = 1; taken(candidate); ++n)
        {
            if (n > maxUniqueSuffix)
            {
                throw std::runtime_error("No free file name for '" + stem + suffix + "' in '" + pathToUtf8(directory) + "'");
            }
            candidate = directory / pathFromUtf8(stem + "_" + std::to_string(n) + suffix);
        }
        return candidate;
    }

    // ------------------------------------------------------------------
    // Engine state
    // ------------------------------------------------------------------

    std::vector<services::TerrainSummary> listTerrains()
    {
        return events::EventDispatcher::instance().query(authoring::ListTerrainsQuery{});
    }

    std::optional<std::vector<services::TerrainSummary>> tryListTerrains()
    {
        try
        {
            return listTerrains();
        }
        catch (const std::runtime_error&)
        {
            return std::nullopt;
        }
    }

    bool isTerrainSaveLocked()
    {
        try
        {
            return events::EventDispatcher::instance().query(authoring::IsTerrainSaveLockedQuery{});
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    bool isWorldModeActive()
    {
        try
        {
            return events::EventDispatcher::instance().query(events::world::IsWorldModeQuery{});
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    std::optional<std::string> terrainEditBlocker(std::string_view action)
    {
        if (events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{}))
        {
            return "Cannot " + std::string(action) + " in Play mode; call play_stop first";
        }
        if (isTerrainSaveLocked())
        {
            return "Cannot " + std::string(action) + " while a terrain save is in progress (the editor's terrain "
                   "panel or another tool is writing a .vfTerrain); retry in a moment";
        }
        return std::nullopt;
    }

    services::TerrainSummary resolveTerrainArg(const ArgReader& reader,
                                               const std::vector<services::TerrainSummary>& terrains,
                                               bool requireLive, bool acceptTileId)
    {
        auto findTerrain = [&terrains](uint64_t id) -> const services::TerrainSummary*
        {
            for (const services::TerrainSummary& terrain : terrains)
            {
                if (terrain.entity.id == id)
                {
                    return &terrain;
                }
            }
            return nullptr;
        };
        auto checked = [requireLive](const services::TerrainSummary& terrain) -> services::TerrainSummary
        {
            if (requireLive && !terrain.live)
            {
                throw std::runtime_error(shellError(terrain));
            }
            return terrain;
        };

        std::vector<const services::TerrainSummary*> all;
        std::vector<const services::TerrainSummary*> live;
        for (const services::TerrainSummary& terrain : terrains)
        {
            all.push_back(&terrain);
            if (terrain.live)
            {
                live.push_back(&terrain);
            }
        }

        if (reader.has("terrain"))
        {
            const uint32_t id = reader.requireEntity("terrain");
            if (const services::TerrainSummary* terrain = findTerrain(id))
            {
                return checked(*terrain);
            }

            // Not a terrain itself: a tile id stands for the terrain it belongs to.
            events::scene::GetEntityQuery query;
            query.entity.id = id;
            const std::optional<services::EntityData> entity = events::EventDispatcher::instance().query(query);
            if (entity.has_value() && entity->hasComponent(services::ComponentTypeId::TerrainTile) &&
                entity->parent.has_value())
            {
                if (const services::TerrainSummary* parent = findTerrain(entity->parent->id))
                {
                    if (!acceptTileId)
                    {
                        throw std::runtime_error("Entity " + std::to_string(id) + " is a tile of terrain " +
                                                 describeTerrain(*parent) + "; pass the terrain id " +
                                                 idText(parent->entity));
                    }
                    return checked(*parent);
                }
            }

            std::string message = entity.has_value()
                ? "Entity " + std::to_string(id) + " ('" + entity->name + "') is not a terrain"
                : "Entity " + std::to_string(id) + " does not exist";
            message += all.empty()
                ? "; the scene has no terrain (create one with terrain_create)"
                : "; terrains: " + describeAll(all) + " (see terrain_get_info)";
            throw std::runtime_error(message);
        }

        if (live.size() == 1)
        {
            return *live.front();
        }
        if (live.size() > 1)
        {
            throw std::runtime_error("The scene has " + std::to_string(live.size()) + " terrains (" + describeAll(live) +
                                     "); pass 'terrain' to choose one");
        }
        if (all.empty())
        {
            throw std::runtime_error("No terrain in the scene; create one with terrain_create");
        }
        if (all.size() == 1)
        {
            return checked(*all.front());
        }
        throw std::runtime_error("No terrain in the scene has data loaded (empty shells: " + describeAll(all) +
                                 "); remove them with terrain_delete and create a new terrain with terrain_create");
    }

    // ------------------------------------------------------------------
    // Geometry
    // ------------------------------------------------------------------

    uint32_t terrainTilesX(const services::TerrainData& data)
    {
        return data.gridMaxX >= data.gridMinX ? static_cast<uint32_t>(data.gridMaxX - data.gridMinX + 1) : 0u;
    }

    uint32_t terrainTilesZ(const services::TerrainData& data)
    {
        return data.gridMaxZ >= data.gridMinZ ? static_cast<uint32_t>(data.gridMaxZ - data.gridMinZ + 1) : 0u;
    }

    uint32_t terrainVerticesPerTile(const services::TerrainData& data)
    {
        // TerrainService::createTerrain treats an unknown resolution as Low; so does this.
        return data.resolution < ::terrain::TILE_VERTEX_COUNTS.size()
            ? ::terrain::TILE_VERTEX_COUNTS[data.resolution]
            : ::terrain::TILE_VERTEX_COUNTS[0];
    }

    float terrainVertexSpacing(const services::TerrainData& data)
    {
        return data.worldTileSize / static_cast<float>(terrainVerticesPerTile(data) - 1);
    }

    uint32_t terrainVertexSpan(const services::TerrainData& data)
    {
        const uint32_t tiles = std::max(terrainTilesX(data), terrainTilesZ(data));
        return tiles * (terrainVerticesPerTile(data) - 1) + 1;
    }

    uint64_t terrainVertexCount(const services::TerrainData& data)
    {
        const uint64_t vertices = terrainVerticesPerTile(data);
        return static_cast<uint64_t>(terrainTilesX(data)) * terrainTilesZ(data) * vertices * vertices;
    }

    TerrainWorldBounds terrainWorldBounds(const services::TerrainData& data)
    {
        const float size = data.worldTileSize;
        TerrainWorldBounds bounds;
        bounds.lower = glm::vec2(static_cast<float>(data.gridMinX) * size, static_cast<float>(data.gridMinZ) * size);
        bounds.upper = glm::vec2(static_cast<float>(data.gridMaxX + 1) * size, static_cast<float>(data.gridMaxZ + 1) * size);
        return bounds;
    }

    std::string terrainResolutionName(uint8_t resolution)
    {
        switch (resolution)
        {
        case 1: return "medium";
        case 2: return "high";
        default: return "low";
        }
    }

    // ------------------------------------------------------------------
    // Reporting
    // ------------------------------------------------------------------

    std::string describeTerrain(const services::TerrainSummary& terrain)
    {
        return idText(terrain.entity) + " ('" + terrain.name + "')";
    }

    nlohmann::json terrainListJson(const std::vector<services::TerrainSummary>& terrains)
    {
        nlohmann::json list = nlohmann::json::array();
        for (const services::TerrainSummary& terrain : terrains)
        {
            list.push_back({{"id", idJson(terrain.entity)}, {"name", terrain.name}, {"live", terrain.live}});
        }
        return list;
    }

    nlohmann::json terrainLayerJson(const services::TerrainMaterialLayerInfo& layer, uint32_t index,
                                    const std::optional<fs::path>& root)
    {
        return {
            {"index", index},
            {"name", layer.name},
            {"material", layer.materialPath.empty()
                             ? nlohmann::json(nullptr)
                             : nlohmann::json(projectRelativeUtf8(root, terrainEnginePath(root, layer.materialPath)))},
            {"tilingScale", layer.tilingScale},
            {"heightBlend", layer.heightBlend},
            {"heightContrast", layer.heightContrast},
            {"enabled", layer.enabled}
        };
    }

    nlohmann::json terrainInfoJson(const services::TerrainSummary& terrain, const std::optional<fs::path>& root)
    {
        const services::TerrainData& data = terrain.data;
        nlohmann::json out{
            {"terrain", idJson(terrain.entity)},
            {"name", terrain.name},
            {"live", terrain.live}
        };
        out["file"] = data.savePath.empty()
            ? nlohmann::json(nullptr)
            : nlohmann::json(projectRelativeUtf8(root, terrainEnginePath(root, data.savePath)));
        if (!terrain.live)
        {
            out["note"] = "Empty shell: no terrain data is loaded for this entity (its .vfTerrain is missing or failed "
                          "to load). Remove it with terrain_delete and create a new terrain with terrain_create.";
            return out;
        }

        const TerrainWorldBounds bounds = terrainWorldBounds(data);
        out["tiles"] = {{"x", terrainTilesX(data)}, {"z", terrainTilesZ(data)}, {"count", data.tileCount}};
        out["tileSize"] = data.worldTileSize;
        out["resolution"] = terrainResolutionName(data.resolution);
        out["verticesPerTile"] = terrainVerticesPerTile(data);
        out["vertexSpacing"] = terrainVertexSpacing(data);
        out["worldBounds"] = {
            {"min", nlohmann::json::array({bounds.lower.x, bounds.lower.y})},
            {"max", nlohmann::json::array({bounds.upper.x, bounds.upper.y})}
        };
        out["heightLimits"] = {{"min", data.minHeight}, {"max", data.maxHeight}};
        out["material"] = terrainMaterialJson(data, root);
        out["collider"] = terrain.hasCollider
            ? nlohmann::json{
                  {"collisionLayer", data.colliderCollisionLayer},
                  {"friction", data.colliderFriction},
                  {"restitution", data.colliderRestitution}
              }
            : nlohmann::json(nullptr);
        out["unsavedChanges"] = terrainNeedsSave(terrain);
        out["saveInProgress"] = isTerrainSaveLocked();
        out["streamingEnabled"] = terrain.streamingEnabled;
        out["residentHeightTiles"] = terrain.residentHeightTiles;
        out["pendingMeshTiles"] = terrain.pendingMeshTiles;
        out["suggestedView"] = suggestedViewJson(data);
        return out;
    }

    std::string strokeErrorText(const services::TerrainStrokeResult& result, const services::TerrainSummary& terrain)
    {
        using Status = services::TerrainStrokeStatus;
        const std::string message = result.message.empty() ? std::string("the terrain edit was refused") : result.message;

        std::string hint;
        switch (result.status)
        {
        case Status::Ok:
            return message;
        case Status::InvalidArguments:
            hint = "check the arguments against the tool description";
            break;
        case Status::NoTerrain:
            hint = "terrain_get_info lists the terrains; terrain_create makes one";
            break;
        case Status::SaveInProgress:
            hint = "a terrain save holds the brush lock; retry in a moment";
            break;
        case Status::GpuUnavailable:
            hint = "raise, lower, smooth and flatten run on the editor's GPU terrain brush, which is not available; "
                   "terrain_paint_layer and terrain_generate_heightmap still work";
            break;
        case Status::TooMuchWork:
            hint = "use a smaller radius or fewer points, or split the stroke over several calls";
            break;
        case Status::OffTerrain:
        {
            const TerrainWorldBounds bounds = terrainWorldBounds(terrain.data);
            hint = "the terrain covers X " + formatNumber(bounds.lower.x) + " to " + formatNumber(bounds.upper.x) +
                   " and Z " + formatNumber(bounds.lower.y) + " to " + formatNumber(bounds.upper.y) +
                   " (world metres; worldBounds in terrain_get_info)";
            break;
        }
        case Status::NoHeightAtPoint:
            hint = "pass 'height' explicitly, or start the stroke on loaded terrain";
            break;
        }
        return std::string("Terrain edit refused (") + strokeStatusName(result.status) + "): " + message + ". Hint: " + hint;
    }

    // ------------------------------------------------------------------
    // Saving
    // ------------------------------------------------------------------

    std::string terrainSaveReason(const services::TerrainSummary& terrain)
    {
        if (!terrain.live)
        {
            return {};
        }
        if (terrain.data.savePath.empty())
        {
            return "never saved";
        }
        if (terrain.data.saveDirty)
        {
            return "unsaved changes";
        }
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(terrain.data.savePath), ec))
        {
            return "file missing";
        }
        return {};
    }

    bool terrainNeedsSave(const services::TerrainSummary& terrain)
    {
        return !terrainSaveReason(terrain).empty();
    }

    fs::path defaultTerrainSavePath(const std::string& terrainName,
                                    const std::vector<services::TerrainSummary>& terrains,
                                    const fs::path& root, const std::vector<fs::path>& claimed)
    {
        // Never hand out a path another terrain already owns, even when its file is gone.
        std::vector<fs::path> taken = claimed;
        for (const services::TerrainSummary& terrain : terrains)
        {
            if (!terrain.data.savePath.empty())
            {
                taken.emplace_back(terrain.data.savePath);
            }
        }
        return uniqueFilePath(root / "terrains", strutil::toSafeFileStem(terrainName, "Terrain"), ".vfTerrain", taken);
    }

    TerrainSaveReport saveTerrainTargets(const std::vector<TerrainSaveTarget>& targets,
                                         const std::optional<fs::path>& root)
    {
        TerrainSaveReport report;
        if (targets.empty())
        {
            return report;
        }

        std::vector<services::TerrainSaveOutcome> outcomes;
        try
        {
            authoring::SaveTerrainsCommand command;
            for (const TerrainSaveTarget& target : targets)
            {
                std::error_code ec;
                fs::create_directories(target.path.parent_path(), ec);
                if (ec)
                {
                    report.ok = false;
                    report.error = "Cannot create folder '" + projectRelativeUtf8(root, target.path.parent_path()) +
                                   "' for terrain " + idText(target.entity) + ": " + ec.message();
                    return report;
                }
                services::TerrainSaveRequest request;
                request.terrainEntity = target.entity;
                request.path = target.path.string(); // engine paths are narrow
                command.requests.push_back(std::move(request));
            }
            outcomes = events::EventDispatcher::instance().execute(command);
        }
        catch (const std::exception& e)
        {
            report.ok = false;
            report.error = std::string("Terrain save failed: ") + e.what();
            return report;
        }

        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            const TerrainSaveTarget& target = targets[i];
            const std::string where = projectRelativeUtf8(root, target.path);
            if (i >= outcomes.size() || !outcomes[i].success)
            {
                const std::string reason = i < outcomes.size() && !outcomes[i].error.empty()
                    ? outcomes[i].error
                    : std::string("the terrain service reported no result");
                report.error += (report.ok ? "" : "; ") + std::string("terrain ") + idText(target.entity) + " ('" +
                                target.name + "') was not saved to " + where + ": " + reason;
                report.ok = false;
                continue;
            }
            report.saved.push_back({
                {"terrain", idJson(target.entity)},
                {"name", target.name},
                {"path", where},
                {"incremental", outcomes[i].incremental}
            });
        }
        if (!report.ok)
        {
            report.error = "Terrain save failed: " + report.error;
        }
        return report;
    }

    TerrainSaveReport saveUnsavedTerrains()
    {
        TerrainSaveReport report;
        const std::optional<std::vector<services::TerrainSummary>> terrains = tryListTerrains();
        if (!terrains.has_value() || terrains->empty())
        {
            return report;
        }
        if (isWorldModeActive())
        {
            report.warnings.push_back("World mode: terrain belongs to the world's sectors and is saved with the world "
                                      "(Save World in the editor), so no .vfTerrain was written");
            return report;
        }

        std::optional<fs::path> root = tryTerrainProjectRoot();
        std::vector<TerrainSaveTarget> targets;
        std::vector<fs::path> claimed;
        for (const services::TerrainSummary& terrain : *terrains)
        {
            if (!terrain.live)
            {
                report.warnings.push_back("Terrain " + describeTerrain(terrain) + " is an empty shell (no data loaded) "
                                          "and was not saved; terrain_delete removes it");
                continue;
            }
            if (!terrainNeedsSave(terrain))
            {
                continue;
            }

            TerrainSaveTarget target;
            target.entity = terrain.entity;
            target.name = terrain.name;
            if (!terrain.data.savePath.empty())
            {
                target.path = fs::path(terrain.data.savePath);
            }
            else if (root.has_value())
            {
                target.path = defaultTerrainSavePath(terrain.name, *terrains, *root, claimed);
            }
            else
            {
                report.ok = false;
                report.error = "Terrain " + describeTerrain(terrain) + " has never been saved and no project is loaded, "
                               "so it has no default location (terrains/<name>.vfTerrain); open a project first";
                return report;
            }
            claimed.push_back(target.path);
            targets.push_back(std::move(target));
        }

        TerrainSaveReport saved = saveTerrainTargets(targets, root);
        saved.warnings.insert(saved.warnings.begin(), report.warnings.begin(), report.warnings.end());
        return saved;
    }
}
