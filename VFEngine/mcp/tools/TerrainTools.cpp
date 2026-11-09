#include "CoreTools.hpp"
#include "PathSandbox.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../util/ImageEncode.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/terrain/HeightmapGenerationEvents.hpp"
#include "events/terrain/TerrainAuthoringEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/TerrainMaterialAssetEvents.hpp"
#include "string/FileNameSanitize.hpp"
#include "terrain/TerrainHeightBlend.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "terrain/TerrainTypes.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// VK-1653 terrain tools. Every edit goes through the self-describing authoring events
// (TerrainAuthoringEvents.hpp): one call is complete when it returns -- heights, seams, colliders,
// save-dirty state -- and pushes exactly zero or one terrain stroke undo entry, recorded by
// TerrainService itself. Nothing here records undo or opens an undo batch.
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace authoring = events::terrainAuthoring;

        constexpr std::chrono::milliseconds strokeTimeout{30000};
        constexpr std::chrono::milliseconds createTimeout{120000};
        constexpr std::chrono::milliseconds saveTimeout{300000};
        constexpr std::chrono::milliseconds generateTimeout{300000};
        // Leaves room inside generateTimeout for the apply and the preview.
        constexpr std::chrono::milliseconds generationDeadline{240000};
        constexpr std::chrono::milliseconds pollInterval{100};
        constexpr std::chrono::milliseconds applyTimeout{120000};

        constexpr int64_t maxTilesPerAxis = 32;
        constexpr std::size_t maxHeightPoints = 1024;
        constexpr int64_t maxAreaSamples = 32;
        constexpr double maxStrokeAmount = 1000.0;  // metres
        constexpr double maxAbsHeight = 1.0e6;      // metres; heights are clamped to the terrain's limits anyway
        constexpr int64_t maxLayerIndex = ::terrain::MAX_TERRAIN_LAYERS - 1;
        constexpr uint32_t previewWidth = 128;
        constexpr uint32_t defaultHeightmapResolution = 1024;
        constexpr uint32_t minAutoResolution = 512;
        constexpr uint32_t maxAutoResolution = 4096;

        const std::vector<std::string> resolutionNames{"low", "medium", "high"}; // TerrainCreationData::resolution
        const std::vector<std::string> sculptOperations{"raise", "lower", "smooth", "flatten"};
        const std::vector<std::string> falloffNames{"smooth", "linear", "sharp", "constant"};
        const std::vector<std::string> shapeNames{"circle", "square"};
        const std::vector<std::string> paintModes{"paint", "erase"};
        const std::vector<std::string> noiseTypeNames{"perlin", "simplex"};                // procedural::NoiseType
        const std::vector<std::string> fractalTypeNames{"none", "fbm", "ridged", "billowy"}; // procedural::FractalType

        // The heightmap presets plus "custom" (generator defaults + overrides).
        const std::vector<std::string>& presetNames()
        {
            static const std::vector<std::string> names = []
            {
                std::vector<std::string> list;
                for (std::string_view id : heightmapPresetIds())
                {
                    list.emplace_back(id);
                }
                list.emplace_back("custom");
                return list;
            }();
            return names;
        }

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        uint8_t indexOf(const std::vector<std::string>& names, const std::string& name)
        {
            return static_cast<uint8_t>(std::find(names.begin(), names.end(), name) - names.begin());
        }

        ::terrain::BrushFalloff toFalloff(const std::string& name)
        {
            if (name == "constant")
            {
                return ::terrain::BrushFalloff::Constant;
            }
            if (name == "linear")
            {
                return ::terrain::BrushFalloff::Linear;
            }
            if (name == "sharp")
            {
                return ::terrain::BrushFalloff::Sharp;
            }
            return ::terrain::BrushFalloff::Smooth;
        }

        ::terrain::BrushShape toShape(const std::string& name)
        {
            return name == "square" ? ::terrain::BrushShape::Square : ::terrain::BrushShape::Circle;
        }

        authoring::SculptOp toSculptOp(const std::string& name)
        {
            if (name == "lower")
            {
                return authoring::SculptOp::Lower;
            }
            if (name == "smooth")
            {
                return authoring::SculptOp::Smooth;
            }
            if (name == "flatten")
            {
                return authoring::SculptOp::Flatten;
            }
            return authoring::SculptOp::Raise;
        }

        // ------------------------------------------------------------------
        // Arguments
        // ------------------------------------------------------------------

        // A project file that must already exist with `extension` (sandboxed to the asset root).
        fs::path requireProjectFile(const fs::path& root, const std::string& path, std::string_view extension,
                                    const char* argument)
        {
            std::string error;
            auto resolved = resolveInside(root, path, error, extension);
            if (!resolved)
            {
                throw ArgError("argument '" + std::string(argument) + "': " + error);
            }
            std::error_code ec;
            if (!fs::is_regular_file(*resolved, ec))
            {
                throw ArgError("argument '" + std::string(argument) + "': '" + path + "' does not exist");
            }
            return *resolved;
        }

        // A project file to write with `extension` (sandboxed to the asset root); it may not exist yet.
        fs::path resolveProjectTarget(const fs::path& root, const std::string& path, std::string_view extension,
                                      const char* argument)
        {
            std::string error;
            auto resolved = resolveInside(root, path, error, extension);
            if (!resolved)
            {
                throw ArgError("argument '" + std::string(argument) + "': " + error);
            }
            return *resolved;
        }

        // An existing project .vfMat / .vfMatInstance for a terrain layer.
        fs::path requireLayerMaterial(const fs::path& root, const std::string& path)
        {
            std::string error;
            auto resolved = resolveInside(root, path, error);
            if (!resolved)
            {
                throw ArgError("argument 'material': " + error);
            }
            const std::string extension = lowercase(genericPathToUtf8(resolved->extension()));
            if (extension != ".vfmat" && extension != ".vfmatinstance")
            {
                throw ArgError("argument 'material': '" + path + "' must be a .vfMat or .vfMatInstance file "
                               "(material_list shows the project's materials)");
            }
            std::error_code ec;
            if (!fs::is_regular_file(*resolved, ec))
            {
                throw ArgError("argument 'material': '" + path + "' does not exist (see material_list / material_create)");
            }
            return *resolved;
        }

        // The optional layer fields shared by terrain_add_layer and terrain_set_layer.
        services::TerrainMaterialLayerPatch readLayerPatch(const ArgReader& reader)
        {
            services::TerrainMaterialLayerPatch patch;
            if (reader.has("name"))
            {
                std::string name = reader.requireString("name");
                if (name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }
                patch.name = std::move(name);
            }
            if (reader.has("tilingScale"))
            {
                patch.tilingScale = static_cast<float>(reader.requireNumberInRange("tilingScale", 0.01, 100.0));
            }
            if (reader.has("heightContrast"))
            {
                patch.heightContrast = static_cast<float>(
                    reader.requireNumberInRange("heightContrast", 0.0, ::terrain::MAX_HEIGHT_BLEND_CONTRAST));
            }
            if (reader.has("heightBlend"))
            {
                patch.heightBlend = reader.requireBool("heightBlend");
            }
            return patch;
        }

        nlohmann::json layerPatchProperties()
        {
            return {
                {"name", schema::string("Layer name (used by terrain_paint_layer 'layer'). Default: the material's file name.")},
                {"tilingScale", schema::numberRange("Texture tiling multiplier (0.01-100); higher repeats the texture more often. Default 1.", 0.01, 100.0)},
                {"heightBlend", schema::boolean("Blend by the material's height (ORM alpha) for crisp transitions instead of a linear mix. Default false.")},
                {"heightContrast", schema::numberRange("Sharpness of a height blend (0-16). Default 4.", 0.0, ::terrain::MAX_HEIGHT_BLEND_CONTRAST)}
            };
        }

        nlohmann::json strokeProperties()
        {
            return {
                {"terrain", schema::entity("Terrain id from terrain_get_info. Default: the scene's only terrain.")},
                {"points", schema::groundPoints("The stroke as ground points [x, z] in world metres (Y is up). One point = one "
                                                "dab; several = a polyline the brush is swept along.",
                                                1, authoring::MAX_STROKE_POINTS)},
                {"radius", schema::numberRange("Brush radius in metres (0.1-512).", authoring::MIN_BRUSH_RADIUS,
                                               authoring::MAX_BRUSH_RADIUS)},
                {"falloff", schema::enumString("Edge falloff. Default smooth; constant = full strength to the rim.", falloffNames)},
                {"shape", schema::enumString("Footprint shape. Default circle; square gives straight edges.", shapeNames)}
            };
        }

        // ------------------------------------------------------------------
        // Results
        // ------------------------------------------------------------------

        nlohmann::json strokeResultJson(const services::TerrainSummary& terrain, const services::TerrainStrokeResult& result,
                                        const std::string& undoLabel)
        {
            nlohmann::json samples = nlohmann::json::array();
            for (const services::TerrainStrokeSample& sample : result.samples)
            {
                samples.push_back({
                    {"point", nlohmann::json::array({sample.xz.x, sample.xz.y})},
                    {"before", sample.valid ? nlohmann::json(sample.before) : nlohmann::json(nullptr)},
                    {"after", sample.valid ? nlohmann::json(sample.after) : nlohmann::json(nullptr)}
                });
            }

            nlohmann::json warnings = nlohmann::json::array();
            for (const std::string& warning : result.warnings)
            {
                warnings.push_back(warning);
            }
            if (result.closedOpenStroke)
            {
                warnings.push_back("An editor brush stroke that was still open was pushed as its own undo step first");
            }
            if (result.tilesChanged == 0)
            {
                warnings.push_back("Nothing changed, so no undo step was recorded");
            }

            return {
                {"terrain", entityId(terrain.entity)},
                {"dabs", result.dabsApplied},
                {"tilesChanged", result.tilesChanged},
                {"tilesSkipped", result.tilesSkipped},
                {"bounds", {
                    {"min", nlohmann::json::array({result.footprintMin.x, result.footprintMin.y})},
                    {"max", nlohmann::json::array({result.footprintMax.x, result.footprintMax.y})}
                }},
                {"samples", std::move(samples)},
                {"undo", {
                    {"pushed", result.undoPushed},
                    {"label", result.undoLabel.empty() ? undoLabel : result.undoLabel}
                }},
                {"warnings", std::move(warnings)}
            };
        }

        // The terrain's material palette. Throws std::runtime_error when it has none or it cannot be read.
        services::TerrainMaterialInfo requireTerrainMaterial(const services::TerrainSummary& terrain,
                                                             const std::optional<fs::path>& root)
        {
            if (terrain.data.terrainMaterialPath.empty())
            {
                throw std::runtime_error("Terrain " + describeTerrain(terrain) + " has no terrain material, so it has no "
                                         "layers yet; add one with terrain_add_layer (the first becomes base layer 0)");
            }
            const fs::path file = terrainEnginePath(root, terrain.data.terrainMaterialPath);
            events::terrainMaterial::GetTerrainMaterialInfoQuery query;
            query.materialPath = file.string();
            std::optional<services::TerrainMaterialInfo> info = events::EventDispatcher::instance().query(query);
            if (!info.has_value())
            {
                throw std::runtime_error("The terrain material '" + projectRelativeUtf8(root, file) +
                                         "' could not be loaded (see logs_read)");
            }
            return std::move(*info);
        }

        std::size_t activeLayers(const services::TerrainMaterialInfo& material)
        {
            return std::min<std::size_t>(material.activeLayerCount, material.layers.size());
        }

        std::string layerListText(const services::TerrainMaterialInfo& material)
        {
            std::string out;
            for (std::size_t i = 0; i < activeLayers(material); ++i)
            {
                out += (out.empty() ? "" : ", ") + std::to_string(i) + " '" + material.layers[i].name + "'";
            }
            return out.empty() ? std::string("none") : out;
        }

        uint32_t layerByIndex(const services::TerrainMaterialInfo& material, int64_t index)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= activeLayers(material))
            {
                throw std::runtime_error("Layer " + std::to_string(index) + " does not exist: the terrain material has " +
                                         std::to_string(activeLayers(material)) + " layer(s) (" + layerListText(material) +
                                         "); add one with terrain_add_layer");
            }
            return static_cast<uint32_t>(index);
        }

        uint32_t layerByName(const services::TerrainMaterialInfo& material, const std::string& name)
        {
            std::vector<uint32_t> matches;
            for (std::size_t i = 0; i < activeLayers(material); ++i)
            {
                if (lowercase(material.layers[i].name) == lowercase(name))
                {
                    matches.push_back(static_cast<uint32_t>(i));
                }
            }
            if (matches.size() == 1)
            {
                return matches.front();
            }
            if (matches.empty())
            {
                throw std::runtime_error("No layer is named '" + name + "'; the terrain material has: " +
                                         layerListText(material) + ". Pass a layer index, or add the layer with "
                                         "terrain_add_layer");
            }
            std::string indices;
            for (uint32_t match : matches)
            {
                indices += (indices.empty() ? "" : ", ") + std::to_string(match);
            }
            throw std::runtime_error("Layer name '" + name + "' is ambiguous (layers " + indices + "); pass the layer index");
        }

        nlohmann::json layerEditJson(const services::TerrainSummary& terrain, const fs::path& root,
                                     const fs::path& terrainMaterial, const services::TerrainMaterialInfo& material,
                                     uint32_t index)
        {
            nlohmann::json out{
                {"terrain", entityId(terrain.entity)},
                {"terrainMaterial", projectRelativeUtf8(root, terrainMaterial)},
                {"layerCount", material.activeLayerCount}
            };
            out["layer"] = index < material.layers.size()
                ? terrainLayerJson(material.layers[index], index, root)
                : nlohmann::json{{"index", index}};

            // Re-read: assigning a new material marks the terrain file dirty; a palette edit does not.
            bool needsSave = false;
            for (const services::TerrainSummary& current : listTerrains())
            {
                if (current.entity == terrain.entity)
                {
                    needsSave = terrainNeedsSave(current);
                }
            }
            out["terrainNeedsSave"] = needsSave;
            return out;
        }

        nlohmann::json noiseSettingsJson(const services::HeightmapNoiseSettings& settings)
        {
            return {
                {"noiseType", settings.noiseType < noiseTypeNames.size() ? noiseTypeNames[settings.noiseType]
                                                                         : std::to_string(settings.noiseType)},
                {"fractalType", settings.fractalType < fractalTypeNames.size() ? fractalTypeNames[settings.fractalType]
                                                                               : std::to_string(settings.fractalType)},
                {"octaves", settings.octaves},
                {"frequency", settings.frequency},
                {"lacunarity", settings.lacunarity},
                {"persistence", settings.persistence},
                {"heightExponent", settings.heightExponent},
                {"domainWarp", settings.domainWarp},
                {"warpAmplitude", settings.warpAmplitude},
                {"warpFrequency", settings.warpFrequency},
                {"invert", settings.invert},
                {"terracing", settings.terracing},
                {"terraceSteps", settings.terraceSteps}
            };
        }

        // dh/d(axis) from the samples one vertex spacing either side; one-sided at an edge or a hole.
        double slopeAlongAxis(const services::TerrainHeightSample& centre, const services::TerrainHeightSample& plus,
                              const services::TerrainHeightSample& minus, float spacing)
        {
            const double step = static_cast<double>(spacing);
            if (plus.valid && minus.valid)
            {
                return (static_cast<double>(plus.height) - static_cast<double>(minus.height)) / (2.0 * step);
            }
            if (plus.valid)
            {
                return (static_cast<double>(plus.height) - static_cast<double>(centre.height)) / step;
            }
            if (minus.valid)
            {
                return (static_cast<double>(centre.height) - static_cast<double>(minus.height)) / step;
            }
            return 0.0;
        }

        // ------------------------------------------------------------------
        // terrain_get_info
        // ------------------------------------------------------------------

        void registerTerrainGetInfo(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_get_info";
            tool.title = "Terrain info";
            tool.description =
                "Describe the scene's terrain. Units are world metres, Y is up and the ground is the XZ plane; the tile "
                "grid is centred on the origin. 'terrains' lists every terrain {id, name, live} (live=false: an empty "
                "shell whose data failed to load). For the selected terrain ('terrain', default: the only live one) it "
                "also returns tiles {x, z, count}, tileSize, resolution and verticesPerTile, vertexSpacing (metres "
                "between height samples), worldBounds {min:[x,z], max:[x,z]}, heightLimits {min, max}, material "
                "{path, layers:[{index, name, material, tilingScale, heightBlend, heightContrast, enabled}]}, collider, "
                "file (project-relative .vfTerrain), unsavedChanges (scene_save or terrain_save writes them), "
                "saveInProgress, pendingMeshTiles (tiles still rebuilding their mesh: wait for 0 before "
                "viewport_screenshot) and suggestedView {position, lookAt} for camera_set. Entity ids, terrain ids "
                "included, change after scene_load and play_stop: call this again then.";
            tool.inputSchema = schema::object({
                {"terrain", schema::entity("Terrain to describe. Default: the scene's only live terrain.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                reader.optEntity("terrain");

                const std::vector<services::TerrainSummary> terrains = listTerrains();
                const std::optional<fs::path> root = tryTerrainProjectRoot();
                const auto liveCount = std::count_if(terrains.begin(), terrains.end(),
                                                     [](const services::TerrainSummary& terrain) { return terrain.live; });

                nlohmann::json out;
                if (reader.has("terrain") || liveCount == 1)
                {
                    out = terrainInfoJson(resolveTerrainArg(reader, terrains, false), root);
                }
                else
                {
                    out = nlohmann::json{{"terrain", nullptr}};
                    out["note"] = terrains.empty()
                        ? "No terrain in the scene; create one with terrain_create"
                        : liveCount == 0
                            ? "No terrain has data loaded (empty shells); remove them with terrain_delete and create "
                              "one with terrain_create"
                            : "The scene has several terrains; pass 'terrain' to describe one";
                }
                out["terrains"] = terrainListJson(terrains);
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // terrain_create
        // ------------------------------------------------------------------

        void registerTerrainCreate(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_create";
            tool.title = "Create terrain";
            tool.description =
                "Create a terrain: a grid of tilesX x tilesZ square tiles, tileSize metres each, centred on the world "
                "origin (Y is up), with a physics collider, saved to a .vfTerrain right away so it survives a scene "
                "reload ('path', default terrains/<name>.vfTerrain). 'heightmap' (a project .vfImage, e.g. from "
                "terrain_generate_heightmap apply:false) sets the initial heights, stretched over the whole terrain "
                "between minHeight and maxHeight; without it the terrain is flat at height 0 (then shape it with "
                "terrain_generate_heightmap / terrain_sculpt). 'terrainMaterial' assigns an existing .vfTerrainMat "
                "(otherwise terrain_add_layer creates one). One terrain per scene: refused while a live terrain exists "
                "(terrain_delete it first), in Play mode and in World mode. Budget: tiles x verticesPerTile^2 <= "
                "4,500,000 (verticesPerTile: low 33, medium 65, high 129). Not undoable (terrain_delete removes it). "
                "Returns the terrain_get_info payload plus created:true.";
            tool.inputSchema = schema::object({
                {"name", schema::string("Terrain entity name (also the default file name). Default 'Terrain'.")},
                {"tilesX", schema::integerRange("Tiles along X (1-32). Default 4.", 1, maxTilesPerAxis)},
                {"tilesZ", schema::integerRange("Tiles along Z (1-32). Default 4.", 1, maxTilesPerAxis)},
                {"resolution", schema::enumString("Vertices per tile side: low 33, medium 65, high 129. Default medium.",
                                                  resolutionNames)},
                {"tileSize", schema::numberRange("Tile edge length in metres (8-256). Default 32.", 8.0, 256.0)},
                {"minHeight", schema::numberRange("Lowest height the terrain can reach, metres (-100..0). Default -10.",
                                                  -100.0, 0.0)},
                {"maxHeight", schema::numberRange("Highest height the terrain can reach, metres (0..500). Default 100.",
                                                  0.0, 500.0)},
                {"heightmap", schema::string("Project-relative .vfImage for the initial heights. Optional.")},
                {"terrainMaterial", schema::string("Project-relative .vfTerrainMat to assign. Optional.")},
                {"collider", schema::boolean("Add a physics heightfield collider. Default true.")},
                {"path", schema::string("Project-relative .vfTerrain to save to (must not exist). Default "
                                        "terrains/<name>.vfTerrain, made unique.")}
            });
            tool.timeout = createTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string name = reader.optString("name", "Terrain");
                if (name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }
                const int64_t tilesX = reader.optIntInRange("tilesX", 1, maxTilesPerAxis, 4);
                const int64_t tilesZ = reader.optIntInRange("tilesZ", 1, maxTilesPerAxis, 4);
                const std::string resolution = reader.optEnum("resolution", resolutionNames, "medium");
                const double tileSize = reader.optNumberInRange("tileSize", 8.0, 256.0, 32.0);
                const double minHeight = reader.optNumberInRange("minHeight", -100.0, 0.0, -10.0);
                const double maxHeight = reader.optNumberInRange("maxHeight", 0.0, 500.0, 100.0);
                if (!(maxHeight > minHeight))
                {
                    throw ArgError("argument 'maxHeight' must be greater than 'minHeight'");
                }
                const bool collider = reader.optBool("collider", true);
                const std::string heightmap = reader.optString("heightmap");
                const std::string terrainMaterial = reader.optString("terrainMaterial");
                const std::string path = reader.optString("path");

                const uint8_t resolutionIndex = indexOf(resolutionNames, resolution);
                const uint64_t vertices = ::terrain::TILE_VERTEX_COUNTS[resolutionIndex];
                const uint64_t total = static_cast<uint64_t>(tilesX * tilesZ) * vertices * vertices;
                if (total > authoring::MAX_TERRAIN_VERTICES)
                {
                    throw ArgError("terrain too large: " + std::to_string(tilesX) + " x " + std::to_string(tilesZ) +
                                   " tiles of " + std::to_string(vertices) + "^2 vertices is " + std::to_string(total) +
                                   " vertices, over the " + std::to_string(authoring::MAX_TERRAIN_VERTICES) +
                                   " limit; use fewer tiles or a lower resolution");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                if (dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::error("Cannot create a terrain in Play mode; call play_stop first");
                }
                if (isWorldModeActive())
                {
                    return ToolResult::error("Cannot create a terrain in World mode: there the terrain belongs to the "
                                             "world's sectors and is authored in the editor's World panel");
                }
                const fs::path root = terrainProjectRoot();
                if (dispatcher.query(authoring::IsTerrainCreationPendingQuery{}))
                {
                    return ToolResult::error("The editor's Create Terrain window is still building a terrain; wait for it "
                                             "to finish, then check terrain_get_info");
                }
                if (isTerrainSaveLocked())
                {
                    return ToolResult::error("Cannot create a terrain while a terrain save is in progress; retry in a moment");
                }
                const std::vector<services::TerrainSummary> terrains = listTerrains();
                for (const services::TerrainSummary& existing : terrains)
                {
                    if (existing.live)
                    {
                        return ToolResult::error("The scene already has terrain " + describeTerrain(existing) +
                                                 "; edit it with the terrain_* tools, or remove it with terrain_delete "
                                                 "before creating another");
                    }
                }

                // Every file is checked before anything is created.
                std::string heightmapPath;
                if (!heightmap.empty())
                {
                    heightmapPath = requireProjectFile(root, heightmap, ".vfImage", "heightmap").string();
                    authoring::ProbeHeightmapQuery probe;
                    probe.path = heightmapPath;
                    const services::HeightmapProbeResult probed = dispatcher.query(probe);
                    if (!probed.valid)
                    {
                        return ToolResult::error("Heightmap '" + heightmap + "' cannot be used: " +
                                                 (probed.error.empty() ? std::string("it could not be loaded") : probed.error));
                    }
                }
                const std::string materialPath = terrainMaterial.empty()
                    ? std::string()
                    : requireProjectFile(root, terrainMaterial, ".vfTerrainMat", "terrainMaterial").string();
                fs::path savePath;
                if (path.empty())
                {
                    savePath = defaultTerrainSavePath(name, terrains, root);
                }
                else
                {
                    savePath = resolveProjectTarget(root, path, ".vfTerrain", "path");
                    std::error_code ec;
                    if (fs::exists(savePath, ec))
                    {
                        return ToolResult::error("'" + path + "' already exists; choose another 'path', or omit it for "
                                                 "a unique terrains/<name>.vfTerrain");
                    }
                }

                // Synchronous on purpose: Begin/PollCreateTerrain is one global slot shared with the editor window.
                events::terrain::CreateTerrainCommand create;
                create.config.tilesX = static_cast<int32_t>(tilesX);
                create.config.tilesZ = static_cast<int32_t>(tilesZ);
                create.config.resolution = resolutionIndex;
                create.config.worldTileSize = static_cast<float>(tileSize);
                create.config.minHeight = static_cast<float>(minHeight);
                create.config.maxHeight = static_cast<float>(maxHeight);
                create.config.heightmapPath = heightmapPath;
                const services::EntityHandle created = dispatcher.execute(create);
                if (!created.isValid())
                {
                    return ToolResult::error("CreateTerrain failed (see logs_read)");
                }

                std::vector<std::string> warnings;
                std::string failure;
                try
                {
                    events::scene::SetEntityNameCommand rename;
                    rename.entity = created;
                    rename.newName = name;
                    dispatcher.execute(rename);

                    if (!materialPath.empty())
                    {
                        events::terrain::SetTerrainMaterialPathCommand assign;
                        assign.terrainEntity = created;
                        assign.materialPath = materialPath;
                        dispatcher.execute(assign);
                    }

                    // Before the save: the collider settings are written to the .vfTerrain header.
                    if (collider)
                    {
                        events::physics::AddTerrainColliderCommand addCollider;
                        addCollider.terrainEntity = created;
                        if (!dispatcher.execute(addCollider))
                        {
                            warnings.push_back("The physics collider could not be created (see logs_read); the terrain "
                                               "has none");
                        }
                    }

                    const TerrainSaveReport saved = saveTerrainTargets({TerrainSaveTarget{created, name, savePath}}, root);
                    if (!saved.ok)
                    {
                        failure = saved.error;
                    }
                }
                catch (const std::exception& e)
                {
                    failure = e.what();
                }

                if (!failure.empty())
                {
                    // A terrain that never reached disk would come back from the next scene load as an empty shell.
                    events::terrain::DeleteTerrainCommand rollback;
                    rollback.terrainEntity = created;
                    try
                    {
                        dispatcher.execute(rollback);
                    }
                    catch (const std::exception&)
                    {
                    }
                    return ToolResult::error("The terrain was not created (rolled back): " + failure);
                }

                const std::vector<services::TerrainSummary> after = listTerrains();
                const auto it = std::find_if(after.begin(), after.end(), [&created](const services::TerrainSummary& terrain)
                {
                    return terrain.entity == created;
                });
                nlohmann::json out = it != after.end() ? terrainInfoJson(*it, root) : nlohmann::json{{"terrain", entityId(created)}};
                out["created"] = true;
                out["terrains"] = terrainListJson(after);
                if (!warnings.empty())
                {
                    out["warnings"] = warnings;
                }
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // terrain_generate_heightmap
        // ------------------------------------------------------------------

        struct GenerateArgs
        {
            services::HeightmapGenerationRequest request; // outputPath and resolution are filled later
            std::optional<uint32_t> resolution;           // nullopt = auto
            std::string path;                             // agent path; empty = the default
            bool apply = true;
            float baseHeight = 0.0f;
            float amplitude = 30.0f;
        };

        void readNoiseOverrides(const nlohmann::json& overrides, services::HeightmapGenerationRequest& request)
        {
            static const std::vector<std::string> known{
                "noiseType", "fractalType", "octaves", "frequency", "lacunarity", "persistence", "heightExponent",
                "domainWarp", "warpAmplitude", "warpFrequency", "invert", "terracing", "terraceSteps"};
            if (!overrides.is_object())
            {
                throw ArgError("argument 'overrides' must be an object");
            }
            for (const auto& item : overrides.items())
            {
                const std::string& key = item.key();
                if (std::find(known.begin(), known.end(), key) == known.end())
                {
                    std::string allowed;
                    for (const std::string& name : known)
                    {
                        allowed += (allowed.empty() ? "" : ", ") + name;
                    }
                    throw ArgError("argument 'overrides' has an unknown field '" + key + "' (allowed: " + allowed + ")");
                }
            }

            // Ranges follow the Heightmap Generator window's sliders.
            const ArgReader reader(overrides);
            if (reader.has("noiseType"))
            {
                request.noiseType = indexOf(noiseTypeNames, reader.requireEnum("noiseType", noiseTypeNames));
            }
            if (reader.has("fractalType"))
            {
                request.fractalType = indexOf(fractalTypeNames, reader.requireEnum("fractalType", fractalTypeNames));
            }
            if (reader.has("octaves"))
            {
                request.octaves = static_cast<int>(reader.requireIntInRange("octaves", 1, 16));
            }
            if (reader.has("frequency"))
            {
                request.frequency = static_cast<float>(reader.requireNumberInRange("frequency", 0.001, 0.1));
            }
            if (reader.has("lacunarity"))
            {
                request.lacunarity = static_cast<float>(reader.requireNumberInRange("lacunarity", 1.0, 4.0));
            }
            if (reader.has("persistence"))
            {
                request.persistence = static_cast<float>(reader.requireNumberInRange("persistence", 0.0, 1.0));
            }
            if (reader.has("heightExponent"))
            {
                request.heightExponent = static_cast<float>(reader.requireNumberInRange("heightExponent", 0.1, 5.0));
            }
            if (reader.has("domainWarp"))
            {
                request.domainWarp = reader.requireBool("domainWarp");
            }
            if (reader.has("warpAmplitude"))
            {
                request.warpAmplitude = static_cast<float>(reader.requireNumberInRange("warpAmplitude", 0.0, 200.0));
            }
            if (reader.has("warpFrequency"))
            {
                request.warpFrequency = static_cast<float>(reader.requireNumberInRange("warpFrequency", 0.001, 0.05));
            }
            if (reader.has("invert"))
            {
                request.invert = reader.requireBool("invert");
            }
            if (reader.has("terracing"))
            {
                request.terracing = reader.requireBool("terracing");
            }
            if (reader.has("terraceSteps"))
            {
                request.terraceSteps = static_cast<int>(reader.requireIntInRange("terraceSteps", 2, 64));
            }
        }

        GenerateArgs readGenerateArgs(const ArgReader& reader)
        {
            GenerateArgs out;
            services::HeightmapGenerationRequest& request = out.request;
            request.preset = reader.optEnum("preset", presetNames(), "hills");
            request.seed = static_cast<uint32_t>(
                reader.optIntInRange("seed", 0, static_cast<int64_t>(std::numeric_limits<uint32_t>::max()), 42));
            if (reader.has("resolution"))
            {
                const nlohmann::json& value = reader.raw("resolution");
                if (value.is_string())
                {
                    if (lowercase(value.get<std::string>()) != "auto")
                    {
                        throw ArgError("argument 'resolution' must be \"auto\" or one of 512, 1024, 2048, 4096");
                    }
                }
                else
                {
                    const int64_t resolution = reader.requireInt("resolution");
                    if (resolution != 512 && resolution != 1024 && resolution != 2048 && resolution != 4096)
                    {
                        throw ArgError("argument 'resolution' must be \"auto\" or one of 512, 1024, 2048, 4096 (got " +
                                       std::to_string(resolution) + ")");
                    }
                    out.resolution = static_cast<uint32_t>(resolution);
                }
            }
            request.featureScale = static_cast<float>(reader.optNumberInRange("featureScale", 0.1, 10.0, 1.0));
            request.overwrite = reader.optBool("overwrite", false);
            if (reader.has("overrides"))
            {
                readNoiseOverrides(reader.raw("overrides"), request);
            }
            out.path = reader.optString("path");
            out.apply = reader.optBool("apply", true);
            out.baseHeight = static_cast<float>(reader.optNumberInRange("baseHeight", -10000.0, 10000.0, 0.0));
            out.amplitude = static_cast<float>(reader.optNumberInRange("amplitude", 0.01, 10000.0, 30.0));
            reader.optEntity("terrain");
            return out;
        }

        // The smallest power of two covering the terrain's height samples, within 512..4096; the generator
        // default when there is no terrain to size against.
        uint32_t autoHeightmapResolution(uint32_t vertexSpan)
        {
            if (vertexSpan == 0)
            {
                return defaultHeightmapResolution;
            }
            uint32_t resolution = minAutoResolution;
            while (resolution < vertexSpan && resolution < maxAutoResolution)
            {
                resolution *= 2;
            }
            return resolution;
        }

        // Main thread: project root, refusals, and the terrain to size and (optionally) apply to.
        // {root, terrain?, vertexSpan?, warnings} or {error}.
        nlohmann::json prepareHeightmapGeneration(const nlohmann::json& args, bool apply, float baseHeight, float amplitude)
        {
            const ArgReader reader(args);
            nlohmann::json out{{"root", pathToUtf8(terrainProjectRoot())}, {"warnings", nlohmann::json::array()}};
            if (apply)
            {
                if (auto blocked = terrainEditBlocker("apply a heightmap"))
                {
                    return {{"error", *blocked}};
                }
            }

            const std::vector<services::TerrainSummary> terrains = listTerrains();
            const auto liveCount = std::count_if(terrains.begin(), terrains.end(),
                                                 [](const services::TerrainSummary& terrain) { return terrain.live; });
            if (apply && !reader.has("terrain") && liveCount == 0)
            {
                return {{"error", "terrain_generate_heightmap applies the heightmap to the scene's terrain, but there is "
                                  "none: call terrain_create first, or pass apply:false to only write the .vfImage"}};
            }

            std::optional<services::TerrainSummary> terrain;
            if (apply || reader.has("terrain") || liveCount == 1)
            {
                terrain = resolveTerrainArg(reader, terrains);
            }
            if (!terrain.has_value())
            {
                return out;
            }

            out["vertexSpan"] = terrainVertexSpan(terrain->data);
            if (apply)
            {
                out["terrain"] = static_cast<uint32_t>(terrain->entity.id);
                const uint64_t vertices = terrainVertexCount(terrain->data);
                if (vertices > authoring::MAX_TERRAIN_VERTICES)
                {
                    return {{"error", "Terrain " + describeTerrain(*terrain) + " has " + std::to_string(vertices) +
                                      " vertices, more than a heightmap can be applied to at once (" +
                                      std::to_string(authoring::MAX_TERRAIN_VERTICES) + "); pass apply:false and use the "
                                      "file with terrain_create {heightmap} on a smaller terrain"}};
                }
                if (baseHeight < terrain->data.minHeight || baseHeight + amplitude > terrain->data.maxHeight)
                {
                    out["warnings"].push_back(std::format("Heights outside the terrain's limits ({} to {} m) are clamped; "
                                                          "adjust baseHeight / amplitude to stay inside them",
                                                          terrain->data.minHeight, terrain->data.maxHeight));
                }
            }
            return out;
        }

        // Main thread: applies the finished heightmap. {terrain, tilesChanged, undo, warnings, ...} or {error}.
        nlohmann::json applyGeneratedHeightmap(uint32_t terrainId, const std::string& heightmapPath, float baseHeight,
                                               float amplitude, const std::string& undoLabel)
        {
            // Generation takes seconds: Play mode or a terrain save may have started in the meantime.
            if (auto blocked = terrainEditBlocker("apply the heightmap"))
            {
                return {{"error", *blocked}};
            }
            const nlohmann::json target{{"terrain", terrainId}};
            const ArgReader reader(target);
            services::TerrainSummary terrain;
            try
            {
                terrain = resolveTerrainArg(reader, listTerrains());
            }
            catch (const std::runtime_error& e)
            {
                return {{"error", std::string(e.what())}};
            }

            authoring::ApplyHeightmapCommand command;
            command.terrainEntity = terrain.entity;
            command.heightmapPath = heightmapPath;
            command.baseHeight = baseHeight;
            command.amplitude = amplitude;
            command.undoLabel = undoLabel;
            const services::TerrainStrokeResult result = events::EventDispatcher::instance().execute(command);
            if (result.status != services::TerrainStrokeStatus::Ok)
            {
                return {{"error", strokeErrorText(result, terrain)}};
            }
            return strokeResultJson(terrain, result, undoLabel);
        }

        std::string previewPngBase64(const services::HeightmapJobStatus& status)
        {
            const uint32_t size = status.previewSize;
            if (size == 0 || status.previewRgba.size() < static_cast<std::size_t>(size) * size * 4)
            {
                return {};
            }
            uint32_t width = 0;
            uint32_t height = 0;
            const std::vector<uint8_t> scaled = util::downscaleRgba8(status.previewRgba, size, size, previewWidth, width, height);
            const std::vector<uint8_t> png = util::encodePng(scaled, width, height);
            return png.empty() ? std::string() : util::base64Encode(png.data(), png.size());
        }

        void registerTerrainGenerateHeightmap(ToolRegistry& registry, const ToolContext& context)
        {
            std::string presets;
            for (std::string_view id : heightmapPresetIds())
            {
                presets += (presets.empty() ? "" : ", ") + std::string(id);
            }

            ToolDef tool;
            tool.name = "terrain_generate_heightmap";
            tool.title = "Generate terrain heightmap";
            tool.description =
                "Generate a procedural noise heightmap, write it as a .vfImage ('path', default "
                "terrains/heightmaps/<preset>_<seed>.vfImage, made unique unless overwrite) and, with apply (default "
                "true), replace the WHOLE terrain's heights with it: the image is stretched over the terrain and its "
                "values 0..1 become baseHeight .. baseHeight + amplitude metres (default 0..30), clamped to the "
                "terrain's heightLimits. Applying is ONE undo step ('undo' or Ctrl+Z restores the previous heights). "
                "Presets: " + presets + " (custom = generator defaults); 'overrides' fine-tune any noise setting, in "
                "the preset's units (they are tuned at 4096 px and rescaled to 'resolution'). resolution 'auto' picks "
                "the smallest power of two covering the terrain's height samples (512-4096). featureScale > 1 makes "
                "features larger. Takes seconds (up to minutes at 4096). Returns a 128 px preview image plus {path, "
                "width, height, preset, seed, resolution, effectiveParams (the generator's final settings, frequencies "
                "already scaled to the resolution), durationMs, applied, terrain, undo}. Applying is refused in Play mode.";
            nlohmann::json resolutionSchema{
                {"type", nlohmann::json::array({"string", "integer"})},
                {"description", "\"auto\" (default) or 512, 1024, 2048, 4096 pixels square"}
            };
            tool.inputSchema = schema::object({
                {"preset", schema::enumString("Noise preset. Default hills.", presetNames())},
                {"seed", schema::integerRange("Random seed. Default 42.", 0, static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))},
                {"resolution", std::move(resolutionSchema)},
                {"featureScale", schema::numberRange("Feature size multiplier (0.1-10). Default 1.", 0.1, 10.0)},
                {"overrides", schema::object({
                    {"noiseType", schema::enumString("Base noise.", noiseTypeNames)},
                    {"fractalType", schema::enumString("Fractal layering.", fractalTypeNames)},
                    {"octaves", schema::integerRange("Noise layers (1-16).", 1, 16)},
                    {"frequency", schema::numberRange("Base frequency per pixel at 4096 px (0.001-0.1).", 0.001, 0.1)},
                    {"lacunarity", schema::numberRange("Frequency gain per octave (1-4).", 1.0, 4.0)},
                    {"persistence", schema::numberRange("Amplitude gain per octave (0-1).", 0.0, 1.0)},
                    {"heightExponent", schema::numberRange("Height curve (0.1-5): >1 deeper valleys, <1 flatter.", 0.1, 5.0)},
                    {"domainWarp", schema::boolean("Warp the noise domain for organic shapes.")},
                    {"warpAmplitude", schema::numberRange("Warp strength (0-200).", 0.0, 200.0)},
                    {"warpFrequency", schema::numberRange("Warp frequency (0.001-0.05).", 0.001, 0.05)},
                    {"invert", schema::boolean("Flip the heights (mountains become canyons).")},
                    {"terracing", schema::boolean("Quantise the heights into steps.")},
                    {"terraceSteps", schema::integerRange("Terrace levels (2-64).", 2, 64)}
                })},
                {"path", schema::string("Project-relative .vfImage to write. Default terrains/heightmaps/<preset>_<seed>.vfImage.")},
                {"overwrite", schema::boolean("Replace an existing file at 'path' (or the default path). Default false.")},
                {"apply", schema::boolean("Apply the heightmap to the terrain (one undo step). Default true.")},
                {"terrain", schema::entity("Terrain to apply to. Default: the scene's only terrain.")},
                {"baseHeight", schema::numberRange("Height in metres of heightmap value 0. Default 0.", -10000.0, 10000.0)},
                {"amplitude", schema::numberRange("Metres from heightmap value 0 to 1. Default 30.", 0.01, 10000.0)}
            });
            // Worker: generation runs for seconds on a background job; only the Begin / Poll / Apply hops
            // touch the main thread, so the editor keeps rendering meanwhile.
            tool.affinity = ThreadAffinity::Worker;
            tool.destructive = true;
            tool.timeout = generateTimeout;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                const ArgReader reader(args);
                const GenerateArgs parsed = readGenerateArgs(reader);
                const std::string undoLabel = "MCP: Apply heightmap (" + parsed.request.preset + ", seed " +
                                              std::to_string(parsed.request.seed) + ")";

                const bool apply = parsed.apply;
                const float baseHeight = parsed.baseHeight;
                const float amplitude = parsed.amplitude;
                const nlohmann::json prepared = context.runOnMain([args, apply, baseHeight, amplitude]() -> nlohmann::json
                {
                    return prepareHeightmapGeneration(args, apply, baseHeight, amplitude);
                });
                if (prepared.contains("error"))
                {
                    return ToolResult::error(prepared.at("error").get<std::string>());
                }
                const fs::path root = pathFromUtf8(prepared.at("root").get<std::string>());
                std::vector<std::string> warnings = prepared.at("warnings").get<std::vector<std::string>>();
                const uint32_t resolution = parsed.resolution.value_or(
                    autoHeightmapResolution(prepared.value("vertexSpan", 0u)));

                fs::path output;
                if (!parsed.path.empty())
                {
                    output = resolveProjectTarget(root, parsed.path, ".vfImage", "path");
                    std::error_code ec;
                    if (!parsed.request.overwrite && fs::exists(output, ec))
                    {
                        return ToolResult::error("'" + parsed.path + "' already exists; pass overwrite:true to replace "
                                                 "it, or another 'path'");
                    }
                }
                else
                {
                    const fs::path directory = root / "terrains" / "heightmaps";
                    const std::string stem = parsed.request.preset + "_" + std::to_string(parsed.request.seed);
                    output = parsed.request.overwrite ? directory / pathFromUtf8(stem + ".vfImage")
                                                      : uniqueFilePath(directory, stem, ".vfImage");
                }
                std::error_code ec;
                fs::create_directories(output.parent_path(), ec);
                if (ec)
                {
                    return ToolResult::error("Cannot create folder '" + projectRelativeUtf8(root, output.parent_path()) +
                                             "': " + ec.message());
                }
                const std::string relativeOutput = projectRelativeUtf8(root, output);
                // HeightmapGenerationHandler reads outputPath as UTF-8; the terrain service loads the
                // heightmap through a narrow engine path like every other engine path. Both are fixed
                // before anything is generated, so an unrepresentable path fails up front.
                const std::string applyPath = apply ? output.string() : std::string();

                services::HeightmapGenerationRequest request = parsed.request;
                request.outputPath = pathToUtf8(output);
                request.resolution = resolution;

                const nlohmann::json started = context.runOnMain([request]() -> nlohmann::json
                {
                    events::heightmapGeneration::BeginHeightmapGenerationCommand command;
                    command.request = request;
                    const services::HeightmapJobStart start = events::EventDispatcher::instance().execute(command);
                    return {{"accepted", start.accepted}, {"jobId", start.jobId}, {"error", start.error}};
                });
                if (!started.at("accepted").get<bool>())
                {
                    const std::string why = started.at("error").get<std::string>();
                    return ToolResult::error("Heightmap generation was refused: " + (why.empty() ? std::string("no reason given") : why));
                }
                const uint64_t jobId = started.at("jobId").get<uint64_t>();

                // The preview is not JSON: each poll writes into a fresh slot it shares by value, so a poll
                // that runs after its caller timed out never writes into a later poll's slot.
                std::shared_ptr<services::HeightmapJobStatus> status;
                const auto deadline = std::chrono::steady_clock::now() + generationDeadline;
                while (true)
                {
                    auto slot = std::make_shared<services::HeightmapJobStatus>();
                    context.runOnMain([slot, jobId]() -> nlohmann::json
                    {
                        events::heightmapGeneration::PollHeightmapGenerationCommand command;
                        command.jobId = jobId;
                        *slot = events::EventDispatcher::instance().execute(command);
                        return static_cast<int>(slot->state);
                    });
                    if (slot->state != services::HeightmapJobState::Running)
                    {
                        status = std::move(slot);
                        break;
                    }
                    if (std::chrono::steady_clock::now() >= deadline)
                    {
                        return ToolResult::error("Heightmap generation did not finish within " +
                                                 std::to_string(generationDeadline.count() / 1000) + " s; it keeps "
                                                 "running in the editor and writes " + relativeOutput + " when done");
                    }
                    std::this_thread::sleep_for(pollInterval);
                }
                if (status->state == services::HeightmapJobState::Failed)
                {
                    return ToolResult::error("Heightmap generation failed: " +
                                             (status->error.empty() ? std::string("no reason given") : status->error));
                }
                if (status->state != services::HeightmapJobState::Done)
                {
                    return ToolResult::error("The heightmap generation job was lost (the editor has no result for it); "
                                             "call terrain_generate_heightmap again");
                }

                nlohmann::json applied = nullptr;
                if (apply)
                {
                    const uint32_t terrainId = prepared.at("terrain").get<uint32_t>();
                    applied = context.runOnMain([terrainId, applyPath, baseHeight, amplitude, undoLabel]() -> nlohmann::json
                    {
                        return applyGeneratedHeightmap(terrainId, applyPath, baseHeight, amplitude, undoLabel);
                    }, applyTimeout);
                    if (applied.contains("error"))
                    {
                        return ToolResult::error("The heightmap was written to " + relativeOutput + " but NOT applied: " +
                                                 applied.at("error").get<std::string>());
                    }
                    for (const nlohmann::json& warning : applied.at("warnings"))
                    {
                        warnings.push_back(warning.get<std::string>());
                    }
                }

                nlohmann::json structured{
                    {"path", relativeOutput},
                    {"width", status->width},
                    {"height", status->height},
                    {"preset", request.preset},
                    {"seed", request.seed},
                    {"resolution", resolution},
                    {"effectiveParams", noiseSettingsJson(status->effective)},
                    {"durationMs", status->durationMs},
                    {"applied", apply}
                };
                std::string text = "Generated " + relativeOutput + " (" + std::to_string(status->width) + "x" +
                                   std::to_string(status->height) + ", preset " + request.preset + ", seed " +
                                   std::to_string(request.seed) + ")";
                if (apply)
                {
                    structured["terrain"] = applied.at("terrain");
                    structured["tilesChanged"] = applied.at("tilesChanged");
                    structured["undo"] = applied.at("undo");
                    text += " and applied it to terrain " + applied.at("terrain").dump() + " as one undo step";
                }
                else
                {
                    structured["terrain"] = nullptr;
                    text += "; not applied (apply:false)";
                }

                const std::string png = previewPngBase64(*status);
                if (png.empty())
                {
                    warnings.push_back("No preview image was produced");
                }
                structured["warnings"] = warnings;
                text += ".";
                if (png.empty())
                {
                    return ToolResult::ok(std::move(structured), std::move(text));
                }
                return ToolResult::image(png, "image/png", std::move(structured), std::move(text));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // terrain_sculpt
        // ------------------------------------------------------------------

        void registerTerrainSculpt(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_sculpt";
            tool.title = "Sculpt terrain";
            tool.description =
                "Sculpt the terrain along a stroke. Units: world metres, Y is up, ground points are [x, z] on the XZ "
                "plane and the terrain grid is centred on the origin (worldBounds in terrain_get_info). One point = one "
                "dab; several = a polyline the brush is swept along. operation: raise / lower move the surface by "
                "'amount' metres at the stroke centreline (normalised: a long stroke does not pile up more than a "
                "short one); smooth blends toward the local average ('strength' 0-1, default 0.5, repeated 'passes' "
                "times); flatten pulls toward 'height' (default: the height under the first point) with 'strength' "
                "(default 1 = reach it). The endpoints of a stroke get only about 62% of 'amount' (their dabs have "
                "neighbours on one side), so extend a stroke about one radius beyond the area that must reach full "
                "height. For a level play area use flatten with falloff 'constant' and shape 'square'. Each call is ONE "
                "undo step ('undo' or Ctrl+Z). Returns {terrain, dabs, tilesChanged, bounds, targetHeight (flatten), "
                "samples:[{point, before, after}] for the first 16 points, warnings, undo:{pushed, label}}. Not allowed "
                "in Play mode.";
            nlohmann::json properties = strokeProperties();
            properties["operation"] = schema::enumString("What the brush does.", sculptOperations);
            properties["amount"] = schema::numberRange("raise / lower (required there): metres to move the stroke "
                                                       "centreline, > 0.", 0.001, maxStrokeAmount);
            properties["height"] = schema::number("flatten: target height in metres. Default: the height under the first point.");
            properties["strength"] = schema::numberRange("smooth (default 0.5) / flatten (default 1): blend per dab, 0-1.", 0.0, 1.0);
            properties["passes"] = schema::integerRange("smooth: repetitions over the whole stroke (1-64). Default 1.", 1,
                                                        authoring::MAX_SMOOTH_PASSES);
            tool.inputSchema = schema::object(std::move(properties), {"operation", "points", "radius"});
            tool.timeout = strokeTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string operation = reader.requireEnum("operation", sculptOperations);

                authoring::SculptTerrainStrokeCommand command;
                command.op = toSculptOp(operation);
                command.points = reader.requireGroundPoints("points", 1, authoring::MAX_STROKE_POINTS);
                command.radius = static_cast<float>(
                    reader.requireNumberInRange("radius", authoring::MIN_BRUSH_RADIUS, authoring::MAX_BRUSH_RADIUS));
                command.falloff = toFalloff(reader.optEnum("falloff", falloffNames, "smooth"));
                command.shape = toShape(reader.optEnum("shape", shapeNames, "circle"));
                switch (command.op)
                {
                case authoring::SculptOp::Raise:
                case authoring::SculptOp::Lower:
                    if (!reader.has("amount"))
                    {
                        throw ArgError("operation '" + operation + "' needs 'amount' (metres to move the stroke centreline)");
                    }
                    command.amount = static_cast<float>(reader.requireNumberInRange("amount", 0.001, maxStrokeAmount));
                    break;
                case authoring::SculptOp::Smooth:
                    command.strength = static_cast<float>(reader.optNumberInRange("strength", 0.0, 1.0, 0.5));
                    command.passes = static_cast<uint32_t>(
                        reader.optIntInRange("passes", 1, authoring::MAX_SMOOTH_PASSES, 1));
                    break;
                case authoring::SculptOp::Flatten:
                    command.strength = static_cast<float>(reader.optNumberInRange("strength", 0.0, 1.0, 1.0));
                    if (reader.has("height"))
                    {
                        command.targetHeight = static_cast<float>(
                            reader.requireNumberInRange("height", -maxAbsHeight, maxAbsHeight));
                    }
                    break;
                }
                reader.optEntity("terrain");
                command.undoLabel = "MCP: Sculpt terrain (" + operation + ")";

                if (auto blocked = terrainEditBlocker("sculpt terrain"))
                {
                    return ToolResult::error(*blocked);
                }
                const services::TerrainSummary terrain = resolveTerrainArg(reader, listTerrains());
                command.terrainEntity = terrain.entity;

                const services::TerrainStrokeResult result = events::EventDispatcher::instance().execute(command);
                if (result.status != services::TerrainStrokeStatus::Ok)
                {
                    return ToolResult::error(strokeErrorText(result, terrain));
                }

                nlohmann::json out = strokeResultJson(terrain, result, command.undoLabel);
                out["operation"] = operation;
                if (command.op == authoring::SculptOp::Flatten)
                {
                    out["targetHeight"] = result.flattenTarget;
                }
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // terrain_paint_layer
        // ------------------------------------------------------------------

        void registerTerrainPaintLayer(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_paint_layer";
            tool.title = "Paint terrain layer";
            tool.description =
                "Paint (mode 'paint') or erase (mode 'erase') a terrain material layer along a stroke of ground points "
                "[x, z] (world metres; points, radius, falloff and shape work as in terrain_sculpt). 'layer' is a layer "
                "index or name from terrain_get_info / terrain_add_layer; layer 0 is the base layer that covers the "
                "terrain at first. 'strength' (0-1, default 1) is the weight added (erase: removed) along the stroke "
                "centreline; the other layers are renormalised. A tile blends at most 8 layers: painting a 9th layer "
                "into a tile skips that tile with a warning, unless allowChannelEviction=true (then the tile's "
                "least-used layer is replaced). Each call is ONE undo step. Not allowed in Play mode.";
            nlohmann::json properties = strokeProperties();
            properties["layer"] = {
                {"type", nlohmann::json::array({"integer", "string"})},
                {"description", "Layer index (0 = base layer) or layer name."}
            };
            properties["strength"] = schema::numberRange("Weight added (erase: removed) at the centreline, 0-1. Default 1.", 0.0, 1.0);
            properties["mode"] = schema::enumString("paint (default) or erase.", paintModes);
            properties["allowChannelEviction"] = schema::boolean(
                "Let a 9th layer replace a tile's least-used layer instead of skipping the tile. Default false.");
            tool.inputSchema = schema::object(std::move(properties), {"layer", "points", "radius"});
            tool.timeout = strokeTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const nlohmann::json& layerArg = reader.raw("layer");
                std::optional<int64_t> layerIndex;
                std::string layerName;
                if (layerArg.is_string())
                {
                    layerName = layerArg.get<std::string>();
                    if (layerName.empty())
                    {
                        throw ArgError("argument 'layer' must not be empty");
                    }
                }
                else if (layerArg.is_number())
                {
                    layerIndex = reader.requireIntInRange("layer", 0, maxLayerIndex);
                }
                else
                {
                    throw ArgError("argument 'layer' must be a layer index (integer) or a layer name (string)");
                }

                const std::string mode = reader.optEnum("mode", paintModes, "paint");
                authoring::PaintTerrainLayerStrokeCommand command;
                command.op = mode == "erase" ? authoring::PaintOp::Erase : authoring::PaintOp::Paint;
                command.points = reader.requireGroundPoints("points", 1, authoring::MAX_STROKE_POINTS);
                command.radius = static_cast<float>(
                    reader.requireNumberInRange("radius", authoring::MIN_BRUSH_RADIUS, authoring::MAX_BRUSH_RADIUS));
                command.strength = static_cast<float>(reader.optNumberInRange("strength", 0.0, 1.0, 1.0));
                command.falloff = toFalloff(reader.optEnum("falloff", falloffNames, "smooth"));
                command.shape = toShape(reader.optEnum("shape", shapeNames, "circle"));
                command.allowChannelEviction = reader.optBool("allowChannelEviction", false);
                reader.optEntity("terrain");

                if (auto blocked = terrainEditBlocker("paint terrain"))
                {
                    return ToolResult::error(*blocked);
                }
                const services::TerrainSummary terrain = resolveTerrainArg(reader, listTerrains());
                const services::TerrainMaterialInfo material = requireTerrainMaterial(terrain, tryTerrainProjectRoot());
                const uint32_t index = layerIndex.has_value() ? layerByIndex(material, *layerIndex)
                                                              : layerByName(material, layerName);
                const std::string& indexName = material.layers[index].name;
                const std::string label = indexName.empty() ? "layer " + std::to_string(index) : indexName;

                command.terrainEntity = terrain.entity;
                command.layerIndex = index;
                command.undoLabel = std::string(command.op == authoring::PaintOp::Erase ? "MCP: Erase terrain layer ("
                                                                                       : "MCP: Paint terrain layer (") +
                                    label + ")";

                const services::TerrainStrokeResult result = events::EventDispatcher::instance().execute(command);
                if (result.status != services::TerrainStrokeStatus::Ok)
                {
                    return ToolResult::error(strokeErrorText(result, terrain));
                }

                nlohmann::json out = strokeResultJson(terrain, result, command.undoLabel);
                out["mode"] = mode;
                out["layer"] = {{"index", index}, {"name", indexName}};
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // terrain_add_layer / terrain_set_layer
        // ------------------------------------------------------------------

        void registerTerrainAddLayer(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_add_layer";
            tool.title = "Add terrain layer";
            tool.description =
                "Add a material layer to the terrain's material palette (.vfTerrainMat). 'material' is a project "
                ".vfMat or .vfMatInstance (material_list); use textured materials - untextured ones look alike. If the "
                "terrain has no terrain material yet, one is created at terrains/<terrain name>.vfTerrainMat, assigned "
                "to the terrain, and this material becomes layer 0, the base layer covering the whole terrain; later "
                "calls append layers 1, 2, ... that you then paint with terrain_paint_layer. The palette file is saved "
                "immediately; when terrainNeedsSave is true the terrain itself still needs scene_save or terrain_save. "
                "Not undoable. Returns {terrain, terrainMaterial, created, layer:{index, name, material, ...}, "
                "layerCount, terrainNeedsSave}.";
            nlohmann::json properties = layerPatchProperties();
            properties["terrain"] = schema::entity("Terrain id. Default: the scene's only terrain.");
            properties["material"] = schema::string("Project-relative .vfMat / .vfMatInstance for the layer.");
            tool.inputSchema = schema::object(std::move(properties), {"material"});
            tool.timeout = strokeTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string material = reader.requireString("material");
                if (material.empty())
                {
                    throw ArgError("argument 'material' must not be empty");
                }
                services::TerrainMaterialLayerPatch patch = readLayerPatch(reader);
                reader.optEntity("terrain");

                if (auto blocked = terrainEditBlocker("add a terrain layer"))
                {
                    return ToolResult::error(*blocked);
                }
                const fs::path root = terrainProjectRoot();
                const fs::path materialFile = requireLayerMaterial(root, material);
                patch.materialPath = materialFile.string();
                if (!patch.name.has_value())
                {
                    patch.name = pathToUtf8(materialFile.stem());
                }
                const services::TerrainSummary terrain = resolveTerrainArg(reader, listTerrains());
                auto& dispatcher = events::EventDispatcher::instance();

                fs::path terrainMaterial;
                services::TerrainMaterialInfo info;
                uint32_t index = 0;
                const bool created = terrain.data.terrainMaterialPath.empty();
                if (created)
                {
                    const fs::path directory = root / "terrains";
                    std::error_code ec;
                    fs::create_directories(directory, ec);
                    if (ec)
                    {
                        return ToolResult::error("Cannot create folder 'terrains': " + ec.message());
                    }

                    events::terrainMaterial::CreateTerrainMaterialAssetCommand create;
                    create.directory = directory.string();
                    create.name = strutil::toSafeFileStem(terrain.name, "Terrain");
                    create.baseLayer = patch;
                    const services::CreateTerrainMaterialAssetResult made = dispatcher.execute(create);
                    if (!made.success)
                    {
                        return ToolResult::error("Could not create the terrain material: " + made.error);
                    }
                    terrainMaterial = fs::path(made.path);

                    events::terrain::SetTerrainMaterialPathCommand assign;
                    assign.terrainEntity = terrain.entity;
                    assign.materialPath = made.path;
                    dispatcher.execute(assign);

                    events::terrainMaterial::GetTerrainMaterialInfoQuery query;
                    query.materialPath = made.path;
                    std::optional<services::TerrainMaterialInfo> loaded = dispatcher.query(query);
                    if (!loaded.has_value())
                    {
                        return ToolResult::error("The terrain material " + projectRelativeUtf8(root, terrainMaterial) +
                                                 " was created and assigned but could not be read back (see logs_read)");
                    }
                    info = std::move(*loaded);
                }
                else
                {
                    terrainMaterial = terrainEnginePath(root, terrain.data.terrainMaterialPath);
                    events::terrainMaterial::EditTerrainMaterialLayerCommand edit;
                    edit.materialPath = terrainMaterial.string();
                    edit.patch = patch; // no index: append
                    const services::TerrainMaterialEditResult edited = dispatcher.execute(edit);
                    if (!edited.success)
                    {
                        return ToolResult::error("Could not add the layer to " + projectRelativeUtf8(root, terrainMaterial) +
                                                 ": " + edited.error);
                    }
                    info = edited.material;
                    index = edited.index;
                }

                nlohmann::json out = layerEditJson(terrain, root, terrainMaterial, info, index);
                out["created"] = created;
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        void registerTerrainSetLayer(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_set_layer";
            tool.title = "Change terrain layer";
            tool.description =
                "Change an existing layer of the terrain's material palette, by 'index' (terrain_get_info lists the "
                "layers): its material, name, tilingScale, heightBlend, heightContrast or enabled. Index 0 retextures "
                "the base layer. Only the given fields change. The palette file is saved immediately; not undoable. "
                "Returns {terrain, terrainMaterial, layer, layerCount, terrainNeedsSave}.";
            nlohmann::json properties = layerPatchProperties();
            properties["terrain"] = schema::entity("Terrain id. Default: the scene's only terrain.");
            properties["index"] = schema::integerRange("Layer index (0 = base layer).", 0, maxLayerIndex);
            properties["material"] = schema::string("Project-relative .vfMat / .vfMatInstance for the layer.");
            properties["enabled"] = schema::boolean("false hides the layer without removing it.");
            tool.inputSchema = schema::object(std::move(properties), {"index"});
            tool.timeout = strokeTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const int64_t index = reader.requireIntInRange("index", 0, maxLayerIndex);
                services::TerrainMaterialLayerPatch patch = readLayerPatch(reader);
                if (reader.has("enabled"))
                {
                    patch.enabled = reader.requireBool("enabled");
                }
                std::string material;
                if (reader.has("material"))
                {
                    material = reader.requireString("material");
                    if (material.empty())
                    {
                        throw ArgError("argument 'material' must not be empty");
                    }
                }
                if (material.empty() && !patch.name && !patch.tilingScale && !patch.heightContrast && !patch.heightBlend &&
                    !patch.enabled)
                {
                    throw ArgError("nothing to change: pass at least one of material, name, tilingScale, heightBlend, "
                                   "heightContrast, enabled");
                }
                reader.optEntity("terrain");

                if (auto blocked = terrainEditBlocker("change a terrain layer"))
                {
                    return ToolResult::error(*blocked);
                }
                const fs::path root = terrainProjectRoot();
                if (!material.empty())
                {
                    patch.materialPath = requireLayerMaterial(root, material).string();
                }
                const services::TerrainSummary terrain = resolveTerrainArg(reader, listTerrains());
                const services::TerrainMaterialInfo current = requireTerrainMaterial(terrain, root);
                const uint32_t layer = layerByIndex(current, index);

                const fs::path terrainMaterial = terrainEnginePath(root, terrain.data.terrainMaterialPath);
                events::terrainMaterial::EditTerrainMaterialLayerCommand edit;
                edit.materialPath = terrainMaterial.string();
                edit.index = layer;
                edit.patch = patch;
                const services::TerrainMaterialEditResult edited = events::EventDispatcher::instance().execute(edit);
                if (!edited.success)
                {
                    return ToolResult::error("Could not change layer " + std::to_string(layer) + " of " +
                                             projectRelativeUtf8(root, terrainMaterial) + ": " + edited.error);
                }
                return ToolResult::ok(layerEditJson(terrain, root, terrainMaterial, edited.material, edited.index));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // terrain_height_at
        // ------------------------------------------------------------------

        void registerTerrainHeightAt(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_height_at";
            tool.title = "Terrain height at points";
            tool.description =
                "Sample the terrain height at ground points [x, z] (world metres) - e.g. to stand entities on the "
                "ground or check that a play area is flat. Pass 'points' (up to 1024) or 'area' {min:[x,z], max:[x,z], "
                "samples} for a samples x samples grid (row-major: z outer, x inner). Returns positions [[x, y, z] | "
                "null] in input order, where y = terrain height + 'offset' - ready for entity_create / "
                "entity_set_transform 'position'; null (also listed in 'invalid') means no height there (outside the "
                "terrain, or no data). includeNormal adds normals [[nx, ny, nz]] and slopeDegrees from neighbouring "
                "height samples. stats {min, max, mean, range, valid} cover the valid terrain heights (offset not "
                "applied). Works on streamed-out tiles too (they are loaded on demand).";
            tool.inputSchema = schema::object({
                {"terrain", schema::entity("Terrain id. Default: the scene's only terrain.")},
                {"points", schema::groundPoints("Ground points [x, z] to sample (up to 1024).", 1, maxHeightPoints)},
                {"area", schema::object({
                    {"min", schema::groundPoint("Area corner [x, z] with the smallest coordinates.")},
                    {"max", schema::groundPoint("Area corner [x, z] with the largest coordinates.")},
                    {"samples", schema::integerRange("Samples per axis, corners included (2-32). Default 8.", 2, maxAreaSamples)}
                }, {"min", "max"})},
                {"offset", schema::number("Metres added to each returned y (e.g. half an entity's height). Default 0.")},
                {"includeNormal", schema::boolean("Also return surface normals and slope angles. Default false.")}
            });
            tool.readOnly = true;
            tool.timeout = strokeTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const bool hasPoints = reader.has("points");
                const bool hasArea = reader.has("area");
                if (hasPoints == hasArea)
                {
                    throw ArgError("pass exactly one of 'points' or 'area'");
                }

                std::vector<glm::vec2> positions;
                int64_t samples = 0;
                if (hasPoints)
                {
                    positions = reader.requireGroundPoints("points", 1, maxHeightPoints);
                }
                else
                {
                    const nlohmann::json& area = reader.raw("area");
                    if (!area.is_object() || !area.contains("min") || !area.contains("max"))
                    {
                        throw ArgError("argument 'area' must be {min: [x, z], max: [x, z], samples}");
                    }
                    const glm::vec2 areaMin = ArgReader::toGroundPoint(area.at("min"), "area.min");
                    const glm::vec2 areaMax = ArgReader::toGroundPoint(area.at("max"), "area.max");
                    if (areaMin.x > areaMax.x || areaMin.y > areaMax.y)
                    {
                        throw ArgError("argument 'area.min' must not exceed 'area.max' on either axis");
                    }
                    samples = ArgReader(area).optIntInRange("samples", 2, maxAreaSamples, 8);
                    const float steps = static_cast<float>(samples - 1);
                    for (int64_t iz = 0; iz < samples; ++iz)
                    {
                        for (int64_t ix = 0; ix < samples; ++ix)
                        {
                            positions.emplace_back(areaMin.x + (areaMax.x - areaMin.x) * (static_cast<float>(ix) / steps),
                                                   areaMin.y + (areaMax.y - areaMin.y) * (static_cast<float>(iz) / steps));
                        }
                    }
                }
                const double offset = reader.optNumberInRange("offset", -maxAbsHeight, maxAbsHeight, 0.0);
                const bool includeNormal = reader.optBool("includeNormal", false);
                reader.optEntity("terrain");

                const services::TerrainSummary terrain = resolveTerrainArg(reader, listTerrains());
                const float spacing = terrainVertexSpacing(terrain.data);

                // includeNormal: four neighbours per point, appended after the points (+x, -x, +z, -z).
                authoring::GetTerrainHeightsQuery query;
                query.terrainEntity = terrain.entity;
                query.pageIn = true;
                query.positions = positions;
                if (includeNormal)
                {
                    query.positions.reserve(positions.size() * 5);
                    for (const glm::vec2& point : positions)
                    {
                        query.positions.emplace_back(point.x + spacing, point.y);
                        query.positions.emplace_back(point.x - spacing, point.y);
                        query.positions.emplace_back(point.x, point.y + spacing);
                        query.positions.emplace_back(point.x, point.y - spacing);
                    }
                }
                const std::vector<services::TerrainHeightSample> heights = events::EventDispatcher::instance().query(query);
                if (heights.size() != query.positions.size())
                {
                    return ToolResult::error("The terrain height query returned " + std::to_string(heights.size()) +
                                             " samples for " + std::to_string(query.positions.size()) + " positions");
                }

                nlohmann::json outPositions = nlohmann::json::array();
                nlohmann::json invalid = nlohmann::json::array();
                nlohmann::json normals = nlohmann::json::array();
                nlohmann::json slopes = nlohmann::json::array();
                double minHeight = std::numeric_limits<double>::max();
                double maxHeight = std::numeric_limits<double>::lowest();
                double sum = 0.0;
                std::size_t valid = 0;
                const std::size_t count = positions.size();
                for (std::size_t i = 0; i < count; ++i)
                {
                    const services::TerrainHeightSample& sample = heights[i];
                    if (!sample.valid)
                    {
                        outPositions.push_back(nullptr);
                        invalid.push_back(i);
                        if (includeNormal)
                        {
                            normals.push_back(nullptr);
                            slopes.push_back(nullptr);
                        }
                        continue;
                    }

                    const double height = static_cast<double>(sample.height);
                    outPositions.push_back(nlohmann::json::array({positions[i].x, height + offset, positions[i].y}));
                    minHeight = std::min(minHeight, height);
                    maxHeight = std::max(maxHeight, height);
                    sum += height;
                    ++valid;

                    if (includeNormal)
                    {
                        const std::size_t neighbours = count + 4 * i;
                        const double dx = slopeAlongAxis(sample, heights[neighbours], heights[neighbours + 1], spacing);
                        const double dz = slopeAlongAxis(sample, heights[neighbours + 2], heights[neighbours + 3], spacing);
                        const glm::dvec3 normal = glm::normalize(glm::dvec3(-dx, 1.0, -dz));
                        normals.push_back(nlohmann::json::array({normal.x, normal.y, normal.z}));
                        slopes.push_back(std::atan(std::sqrt(dx * dx + dz * dz)) * (180.0 / std::numbers::pi));
                    }
                }

                nlohmann::json out{
                    {"terrain", entityId(terrain.entity)},
                    {"positions", std::move(outPositions)},
                    {"invalid", std::move(invalid)},
                    {"offset", offset}
                };
                if (includeNormal)
                {
                    out["normals"] = std::move(normals);
                    out["slopeDegrees"] = std::move(slopes);
                }
                out["stats"] = valid == 0
                    ? nlohmann::json(nullptr)
                    : nlohmann::json{
                          {"min", minHeight},
                          {"max", maxHeight},
                          {"mean", sum / static_cast<double>(valid)},
                          {"range", maxHeight - minHeight},
                          {"valid", valid}
                      };
                if (hasArea)
                {
                    out["samplesPerAxis"] = samples;
                }
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // terrain_save / terrain_delete
        // ------------------------------------------------------------------

        void registerTerrainSave(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_save";
            tool.title = "Save terrain";
            tool.description =
                "Write terrain to its .vfTerrain file (heights, layers, material reference, collider settings). Terrain "
                "data lives only there - the .vfScene just references it - so unsaved terrain edits are lost on "
                "scene_load or play_stop. scene_save already does this for every terrain that needs it; use this tool "
                "to save without the scene or to 'Save As'. Without arguments it saves every terrain with unsaved "
                "changes (a never-saved one to terrains/<name>.vfTerrain). With 'terrain' it saves that one; with "
                "'path' (project-relative .vfTerrain) it saves the terrain there (overwrite:true to replace an existing "
                "file) and that file becomes the terrain's own. Not allowed in Play mode or World mode. Returns "
                "{terrains:[{terrain, name, path, incremental}], warnings} (+ terrain when one was named).";
            tool.inputSchema = schema::object({
                {"terrain", schema::entity("Terrain to save. Default: every terrain with unsaved changes (or, with "
                                           "'path', the only terrain).")},
                {"path", schema::string("Project-relative .vfTerrain to save to (Save As). Optional.")},
                {"overwrite", schema::boolean("Replace an existing file at 'path'. Default false.")}
            });
            tool.timeout = saveTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                reader.optEntity("terrain");
                const std::string path = reader.optString("path");
                if (reader.has("path") && path.empty())
                {
                    throw ArgError("argument 'path' must not be empty");
                }
                const bool overwrite = reader.optBool("overwrite", false);

                if (events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::error("Cannot save terrain in Play mode (it would write the running game's "
                                             "terrain); call play_stop first");
                }
                if (isWorldModeActive())
                {
                    return ToolResult::error("World mode: the terrain belongs to the world's sectors and is saved with "
                                             "the world (Save World in the editor)");
                }
                if (isTerrainSaveLocked())
                {
                    return ToolResult::error("A terrain save is already in progress; retry in a moment");
                }

                if (!reader.has("terrain") && path.empty())
                {
                    const TerrainSaveReport report = saveUnsavedTerrains();
                    if (!report.ok)
                    {
                        return ToolResult::error(report.error);
                    }
                    nlohmann::json out{{"terrains", report.saved}, {"warnings", report.warnings}};
                    if (report.saved.empty())
                    {
                        out["note"] = "Every terrain was already saved";
                    }
                    return ToolResult::ok(std::move(out));
                }

                const fs::path root = terrainProjectRoot();
                const std::vector<services::TerrainSummary> terrains = listTerrains();
                const services::TerrainSummary terrain = resolveTerrainArg(reader, terrains);
                const fs::path ownFile = terrainEnginePath(root, terrain.data.savePath);

                fs::path target;
                if (!path.empty())
                {
                    target = resolveProjectTarget(root, path, ".vfTerrain", "path");
                    for (const services::TerrainSummary& other : terrains)
                    {
                        if (other.entity != terrain.entity &&
                            sameTerrainFile(target, terrainEnginePath(root, other.data.savePath)))
                        {
                            return ToolResult::error("'" + path + "' is the file of terrain " + describeTerrain(other) +
                                                     "; choose another path");
                        }
                    }
                    std::error_code ec;
                    if (!overwrite && !sameTerrainFile(target, ownFile) && fs::exists(target, ec))
                    {
                        return ToolResult::error("'" + path + "' already exists; pass overwrite:true to replace it");
                    }
                }
                else if (!ownFile.empty())
                {
                    target = ownFile;
                }
                else
                {
                    target = defaultTerrainSavePath(terrain.name, terrains, root);
                }

                const TerrainSaveReport report = saveTerrainTargets({TerrainSaveTarget{terrain.entity, terrain.name, target}}, root);
                if (!report.ok)
                {
                    return ToolResult::error(report.error);
                }
                return ToolResult::ok({
                    {"terrain", entityId(terrain.entity)},
                    {"terrains", report.saved},
                    {"warnings", report.warnings}
                });
            };
            registry.add(std::move(tool));
        }

        void registerTerrainDelete(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "terrain_delete";
            tool.title = "Delete terrain";
            tool.description =
                "Remove a terrain (and its tile entities) from the scene. Its .vfTerrain / .vfTerrainMat files stay on "
                "disk. NOT undoable; earlier terrain undo steps for it become no-ops. Also the way to remove an empty "
                "shell terrain. Not allowed in Play mode. Returns {terrain, deleted, name, file}.";
            tool.inputSchema = schema::object({
                {"terrain", schema::entity("Terrain id from terrain_get_info.")}
            }, {"terrain"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                reader.requireEntity("terrain");

                if (auto blocked = terrainEditBlocker("delete a terrain"))
                {
                    return ToolResult::error(*blocked);
                }
                const services::TerrainSummary terrain = resolveTerrainArg(reader, listTerrains(), false, false);

                events::terrain::DeleteTerrainCommand command;
                command.terrainEntity = terrain.entity;
                if (!events::EventDispatcher::instance().execute(command))
                {
                    return ToolResult::error("DeleteTerrain failed for terrain " + describeTerrain(terrain) + " (see logs_read)");
                }

                const std::optional<fs::path> root = tryTerrainProjectRoot();
                return ToolResult::ok({
                    {"terrain", entityId(terrain.entity)},
                    {"deleted", true},
                    {"name", terrain.name},
                    {"file", terrain.data.savePath.empty()
                                 ? nlohmann::json(nullptr)
                                 : nlohmann::json(projectRelativeUtf8(root, terrainEnginePath(root, terrain.data.savePath)))}
                });
            };
            registry.add(std::move(tool));
        }
    }

    void registerTerrainTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerTerrainGetInfo(registry);
        registerTerrainCreate(registry);
        registerTerrainGenerateHeightmap(registry, context);
        registerTerrainSculpt(registry);
        registerTerrainPaintLayer(registry);
        registerTerrainAddLayer(registry);
        registerTerrainSetLayer(registry);
        registerTerrainHeightAt(registry);
        registerTerrainSave(registry);
        registerTerrainDelete(registry);
    }
}
