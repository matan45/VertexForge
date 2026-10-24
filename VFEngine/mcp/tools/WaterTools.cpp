#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "WaterToolSupport.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"
#include "../undo/WaterUndo.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "water/SeaState.hpp"
#include "water/WaterBodyMath.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// VK-1658 water tools: water_get, water_create_body, water_set_body, water_set_params, ocean_enable,
// ocean_set_params. The ocean is the scene's one OceanComponent, read and written whole as an OceanStateData
// (GetOceanStateQuery / SetOceanStateCommand): its isActive is whether the sea SURFACE shows, and a hidden
// ocean still holds the water look, level, sea state and buoyancy tuning every lake and pool uses. Lakes and
// pools are WaterBodyComponent entities (CreateWaterBodyCommand / SetWaterBodyDataCommand) placed by their
// LOCAL transform. Every edit pushes one undo batch (mcp/undo/WaterUndo, plus the generic entity undos for
// bodies) or none when nothing changes; the keys and ranges live in WaterToolSupport.
namespace mcp::tools
{
    namespace
    {
        constexpr std::chrono::milliseconds waterTimeout{30000};

        // A rim more than this far below the surface counts as overflowing (float noise stays quiet).
        constexpr float rimTolerance = 0.01f;

        constexpr const char* hiddenNote =
            "The ocean is hidden (ocean_enable {enabled:true} shows it): it still holds the water look every lake and "
            "pool uses, its level, sea state and buoyancy tuning, but there is no sea surface, no FFT waves (bodies "
            "with waves render flat), no caustics, wet shore or ocean buoyancy.";

        std::string describe(const services::EntityData& entity)
        {
            return std::to_string(toId(entity.handle)) + " ('" + entity.name + "')";
        }

        std::string number(float value)
        {
            return env::detail::floatText(value);
        }

        std::string entityName(const services::EntityHandle& entity)
        {
            const std::optional<services::EntityData> data = findEntity(entity);
            return data.has_value() ? data->name : std::string();
        }

        bool sceneLoadPending()
        {
            try
            {
                return events::EventDispatcher::instance().query(events::scene::IsSceneLoadPendingQuery{});
            }
            catch (const std::runtime_error&)
            {
                return false;  // no scene persistence service: nothing can be loading
            }
        }

        // The tracked ocean and its state; state is nullopt when there is no ocean.
        struct OceanRead
        {
            services::EntityHandle entity;
            std::optional<services::OceanStateData> state;
        };

        OceanRead readOcean()
        {
            OceanRead out;
            out.entity = watertool::trackedOcean();
            if (out.entity.isValid())
            {
                out.state = watertool::readOceanState(out.entity);
            }
            return out;
        }

        bool oceanVisible()
        {
            const OceanRead ocean = readOcean();
            return ocean.state.has_value() && ocean.state->isActive;
        }

        bool anyLiveTerrain()
        {
            const std::optional<std::vector<services::TerrainSummary>> terrains = tryListTerrains();
            return terrains.has_value() &&
                std::any_of(terrains->begin(), terrains->end(), [](const services::TerrainSummary& t) { return t.live; });
        }

        std::vector<std::string> typeNames()
        {
            return {std::string(watertool::bodyTypeNames[0]), std::string(watertool::bodyTypeNames[1])};
        }

        uint32_t typeIndex(const std::string& name)
        {
            return name == watertool::bodyTypeNames[1] ? 1u : 0u;
        }

        // ------------------------------------------------------------------
        // Arguments
        // ------------------------------------------------------------------

        // 'level' (absolute) XOR 'levelAboveTerrain' (relative to a terrain reference).
        struct LevelRequest
        {
            std::optional<float> absolute;
            std::optional<float> aboveTerrain;
        };

        LevelRequest readLevel(const ArgReader& reader, bool required)
        {
            const bool hasLevel = reader.has("level");
            const bool hasAbove = reader.has("levelAboveTerrain");
            if (hasLevel && hasAbove)
            {
                throw ArgError("pass either 'level' (the surface height in world metres) or 'levelAboveTerrain' (metres "
                               "above the terrain), not both");
            }
            if (required && !hasLevel && !hasAbove)
            {
                throw ArgError("pass 'level' (the surface height in world metres) or 'levelAboveTerrain' (metres above "
                               "the terrain under it)");
            }
            LevelRequest out;
            if (hasLevel)
            {
                out.absolute = static_cast<float>(
                    reader.requireNumberInRange("level", -watertool::maxLevel, watertool::maxLevel));
            }
            if (hasAbove)
            {
                out.aboveTerrain = static_cast<float>(reader.requireNumberInRange(
                    "levelAboveTerrain", -watertool::maxLevelAboveTerrain, watertool::maxLevelAboveTerrain));
            }
            return out;
        }

        watertool::TerrainReference readReference(const ArgReader& reader, const LevelRequest& level)
        {
            if (!reader.has("terrainReference"))
            {
                return watertool::TerrainReference::Lowest;
            }
            if (!level.aboveTerrain.has_value())
            {
                throw ArgError("'terrainReference' only applies with 'levelAboveTerrain'");
            }
            const std::vector<std::string> names{std::string(watertool::terrainReferenceNames[0]),
                                                 std::string(watertool::terrainReferenceNames[1]),
                                                 std::string(watertool::terrainReferenceNames[2])};
            const std::string name = reader.requireEnum("terrainReference", names);
            if (name == watertool::terrainReferenceNames[1])
            {
                return watertool::TerrainReference::Center;
            }
            if (name == watertool::terrainReferenceNames[2])
            {
                return watertool::TerrainReference::Rim;
            }
            return watertool::TerrainReference::Lowest;
        }

        std::string_view referenceName(watertool::TerrainReference reference)
        {
            return watertool::terrainReferenceNames[static_cast<std::size_t>(reference)];
        }

        glm::vec2 readHalfExtents(const nlohmann::json& value)
        {
            if (!value.is_array() || value.size() != 2)
            {
                throw ArgError("'halfExtents' must be [halfX, halfZ] in metres (got " + env::detail::valueText(value) +
                               ")");
            }
            return {env::requireFloat(value[0], 0.0, watertool::maxHalfExtent, "halfExtents[0]"),
                    env::requireFloat(value[1], 0.0, watertool::maxHalfExtent, "halfExtents[1]")};
        }

        // ------------------------------------------------------------------
        // Terrain
        // ------------------------------------------------------------------

        // The terrain to sample: the 'terrain' argument, else the scene's only live terrain. When `required`
        // a missing terrain throws (naming terrain_create); otherwise nullopt when the scene has none, there is
        // no terrain service, or several live terrains and no 'terrain' argument.
        std::optional<services::TerrainSummary> samplingTerrain(const ArgReader& reader, bool required)
        {
            const std::optional<std::vector<services::TerrainSummary>> terrains = tryListTerrains();
            if (!terrains.has_value())
            {
                if (required || reader.has("terrain"))
                {
                    throw std::runtime_error("There is no terrain to sample (no terrain service)");
                }
                return std::nullopt;
            }
            if (!required && !reader.has("terrain"))
            {
                const auto live = std::count_if(terrains->begin(), terrains->end(),
                                                [](const services::TerrainSummary& t) { return t.live; });
                if (live != 1)
                {
                    return std::nullopt;
                }
            }
            return resolveTerrainArg(reader, *terrains);
        }

        watertool::FootprintStats sampleFootprint(const services::TerrainSummary& terrain, const glm::vec2& center,
                                                  const glm::vec2& halfExtents)
        {
            const watertool::Footprint footprint =
                watertool::footprintGrid(center, halfExtents, terrainVertexSpacing(terrain.data));
            const std::vector<services::TerrainHeightSample> heights =
                watertool::sampleTerrainHeights(terrain.entity, footprint.points);
            return watertool::footprintStats(footprint, heights);
        }

        // A body's surface from 'levelAboveTerrain' over its footprint.
        float relativeBodyLevel(const std::optional<watertool::FootprintStats>& stats, watertool::TerrainReference reference,
                                float aboveTerrain)
        {
            const std::optional<float> base =
                stats.has_value() ? watertool::referenceHeight(*stats, reference) : std::optional<float>();
            if (!base.has_value())
            {
                throw std::runtime_error("No terrain height was found under the rectangle for terrainReference '" +
                                         std::string(referenceName(reference)) +
                                         "' (is it outside the terrain? terrain_get_info lists its worldBounds); pass "
                                         "an absolute 'level' instead");
            }
            const float level = *base + aboveTerrain;
            if (std::fabs(level) > watertool::maxLevel)
            {
                throw ArgError("the resulting level " + number(level) + " is outside +/-1e6 metres");
            }
            return level;
        }

        // The ocean level 'level' / 'levelAboveTerrain' (+ 'terrainPoint', 'terrain') ask for; nullopt when
        // neither is given. `reference` describes the terrain height it was measured from.
        std::optional<float> requestedOceanLevel(const ArgReader& reader, const LevelRequest& request,
                                                 nlohmann::json& reference)
        {
            if (reader.has("terrainPoint") && !request.aboveTerrain.has_value())
            {
                throw ArgError("'terrainPoint' only applies with 'levelAboveTerrain'");
            }
            if (reader.has("terrain") && !request.aboveTerrain.has_value())
            {
                throw ArgError("'terrain' only applies with 'levelAboveTerrain'");
            }
            if (request.absolute.has_value())
            {
                return request.absolute;
            }
            if (!request.aboveTerrain.has_value())
            {
                return std::nullopt;
            }

            const services::TerrainSummary terrain = *samplingTerrain(reader, true);
            float base = 0.0f;
            if (reader.has("terrainPoint"))
            {
                const glm::vec2 point = reader.requireGroundPoint("terrainPoint");
                const std::vector<services::TerrainHeightSample> heights =
                    watertool::sampleTerrainHeights(terrain.entity, {point});
                if (!heights.front().valid)
                {
                    throw std::runtime_error("terrainPoint [" + number(point.x) + ", " + number(point.y) +
                                             "] has no terrain height (outside the terrain? terrain_get_info lists its "
                                             "worldBounds)");
                }
                base = heights.front().height;
                reference = {{"terrain", entityId(terrain.entity)},
                             {"point", nlohmann::json::array({env::floatJson(point.x), env::floatJson(point.y)})},
                             {"height", env::floatJson(base)}};
            }
            else
            {
                const TerrainWorldBounds bounds = terrainWorldBounds(terrain.data);
                const watertool::FootprintStats stats = sampleFootprint(terrain, (bounds.lower + bounds.upper) * 0.5f,
                                                                        (bounds.upper - bounds.lower) * 0.5f);
                if (!stats.anyHeight())
                {
                    throw std::runtime_error("Terrain " + describeTerrain(terrain) +
                                             " returned no heights; pass an absolute 'level' instead");
                }
                base = stats.min;
                reference = {{"terrain", entityId(terrain.entity)}, {"point", "lowest"}, {"height", env::floatJson(base)}};
            }
            const float level = base + *request.aboveTerrain;
            if (std::fabs(level) > watertool::maxLevel)
            {
                throw ArgError("the resulting level " + number(level) + " is outside +/-1e6 metres");
            }
            return level;
        }

        // ------------------------------------------------------------------
        // Water bodies
        // ------------------------------------------------------------------

        std::optional<services::WaterBodyComponentData> bodyData(const services::EntityHandle& entity)
        {
            events::ocean::GetWaterBodyDataQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }

        services::TransformData requireTransform(const services::EntityHandle& entity)
        {
            events::scene::GetTransformQuery query;
            query.entity = entity;
            const std::optional<services::TransformData> transform = events::EventDispatcher::instance().query(query);
            if (!transform.has_value())
            {
                throw std::runtime_error("Entity " + std::to_string(toId(entity)) + " has no transform");
            }
            return *transform;
        }

        // Without 'depth' the floor sits 1 m below the lowest terrain point under the rectangle, so a crate on the
        // lake bed is still in the water; without terrain samples, the component's own default.
        float defaultDepth(float level, const std::optional<watertool::FootprintStats>& stats)
        {
            if (!stats.has_value() || !stats->anyHeight())
            {
                return services::WaterBodyComponentData{}.depth;
            }
            return std::clamp(level - stats->min + 1.0f, 0.5f, static_cast<float>(watertool::maxBodyDepth));
        }

        std::vector<std::string> bodyWarnings(float level, const std::optional<watertool::FootprintStats>& stats,
                                              const services::WaterBodyComponentData& data, bool visibleOcean)
        {
            std::vector<std::string> warnings;
            if (!(data.halfExtents.x > 0.0f) || !(data.halfExtents.y > 0.0f))
            {
                warnings.push_back("A zero half extent makes an empty rectangle: the body draws and floats nothing.");
            }
            if (stats.has_value())
            {
                if (!stats->anyHeight())
                {
                    warnings.push_back("No terrain height was found under the rectangle: it lies outside the terrain.");
                }
                else
                {
                    if (stats->valid < stats->total)
                    {
                        warnings.push_back(std::to_string(stats->total - stats->valid) + " of " +
                                           std::to_string(stats->total) +
                                           " footprint samples are off the terrain: part of the rectangle overhangs it.");
                    }
                    if (stats->rimMin.has_value() && level > *stats->rimMin + rimTolerance)
                    {
                        warnings.push_back("The surface (level " + number(level) + ") stands " +
                                           number(level - *stats->rimMin) +
                                           " m above the lowest point of the rectangle's edge (" + number(*stats->rimMin) +
                                           "): unless walls hold it (a raised pool), the water shows past the shore "
                                           "there. Lower the level, shrink the rectangle, or use levelAboveTerrain with "
                                           "terrainReference 'rim' and a small negative value.");
                    }
                    if (level < stats->min)
                    {
                        warnings.push_back("The surface (level " + number(level) +
                                           ") is below the terrain everywhere under the rectangle (lowest " +
                                           number(stats->min) + "): the body is hidden under the ground (it shows only "
                                           "through a hole or cave in the terrain).");
                    }
                }
            }
            if (data.bandMask != 0u && !visibleOcean)
            {
                warnings.push_back("'waves' are the ocean's FFT bands, which run only while the ocean is visible: the body "
                                   "renders flat until ocean_enable {enabled:true}.");
            }
            return warnings;
        }

        nlohmann::json halfExtentsSchema(std::string description)
        {
            nlohmann::json out = schema::array({{"type", "number"}, {"minimum", 0.0}, {"maximum", watertool::maxHalfExtent}},
                                               std::move(description));
            out["minItems"] = 2;
            out["maxItems"] = 2;
            return out;
        }

        nlohmann::json wavesSchema(std::string description)
        {
            nlohmann::json names = nlohmann::json::array();
            for (std::string_view name : watertool::bandNames)
            {
                names.push_back(std::string(name));
            }
            return schema::array({{"type", "string"}, {"enum", std::move(names)}}, std::move(description));
        }

        nlohmann::json referenceSchema()
        {
            return schema::enumString("With levelAboveTerrain: what the level is measured from - lowest (the lowest "
                                      "terrain point under the rectangle), center (the height at its centre) or rim (the "
                                      "lowest point on its edge; with a small negative value it fills a basin to just "
                                      "below its lip). Default lowest.",
                                      {"lowest", "center", "rim"});
        }

        // The keys water_create_body and water_set_body share.
        nlohmann::json bodyProperties(bool create)
        {
            return {
                {"center", schema::groundPoint(create ? "Centre of the rectangle [x, z] in world metres (Y is up)"
                                                      : "New centre [x, z] in world metres (moves the entity)")},
                {"halfExtents", halfExtentsSchema("Half size [hx, hz] of the axis-aligned rectangle, metres (0-5000)")},
                {"level", schema::numberRange("Surface height in world metres", -watertool::maxLevel, watertool::maxLevel)},
                {"levelAboveTerrain", schema::numberRange("Surface height in metres above terrainReference (negative = "
                                                          "below it)", -watertool::maxLevelAboveTerrain,
                                                          watertool::maxLevelAboveTerrain)},
                {"terrainReference", referenceSchema()},
                {"name", schema::string(create ? "Entity name. Default 'Lake' or 'Pool'." : "New entity name")},
                {"type", schema::enumString("lake or pool (a label: both behave the same)."
                                            + std::string(create ? " Default lake." : ""), typeNames())},
                {"depth", schema::numberRange(create ? "Metres of water below the surface (its floor). Default: 1 m below "
                                                       "the lowest terrain point under it, 10 m without terrain."
                                                     : "Metres of water below the surface (its floor)",
                                              0.0, watertool::maxBodyDepth)},
                {"waves", wavesSchema("The ocean's FFT bands to run on the body: swell, agitation, ripples (needs a "
                                      "visible ocean)." + std::string(create ? " Default [] = mirror flat." : ""))},
                {"physicsEnabled", schema::boolean("Float dynamic rigid bodies (off = a hole: nothing floats, and the ocean "
                                                   "below does not take over)." +
                                                   std::string(create ? " Default true." : ""))},
                {"active", schema::boolean("Draw and float at all." + std::string(create ? " Default true." : ""))},
                {"terrain", schema::entity("Terrain to sample. Default: the scene's only terrain.")}
            };
        }

        // ------------------------------------------------------------------
        // Ocean
        // ------------------------------------------------------------------

        void weatherNotes(const services::OceanStateData& state, std::vector<std::string>& notes)
        {
            if (!state.weatherDriven)
            {
                return;
            }
            notes.push_back(env::isWeatherEnabled()
                ? "weatherDriven: the live weather sets the sea state (each band's wind, amplitude, choppiness and foam); "
                  "ocean_set_params {weatherDriven:false} hands them back."
                : "weatherDriven is on but the weather is disabled (weather_set): the bands keep their values until it is "
                  "enabled.");
        }

        // Writes `after` over `before` (both the tracked ocean's) as one undo step. False when nothing changed.
        bool writeOceanRecorded(const services::EntityHandle& ocean, const services::OceanStateData& before,
                                const services::OceanStateData& after, const std::string& label)
        {
            const watertool::OceanParts parts = watertool::changedParts(before, after);
            if (parts == 0u)
            {
                return false;
            }
            undo::UndoRecorder recorder(label + " (" + watertool::partsText(parts) + ")");
            try
            {
                watertool::writeOceanState(ocean, after);
                recorder.add(std::make_unique<undo::OceanStateUndo>(toId(ocean), before, after, parts,
                                                                    label + " (" + watertool::partsText(parts) + ")"));
            }
            catch (...)
            {
                recorder.rollback();
                throw;
            }
            recorder.push();
            return true;
        }

        // Creates the ocean with the editor's defaults at `level`, then hands it to `finish` (which may write
        // its state); one undo step. A failure deletes the half-made ocean again.
        template <typename Finish>
        services::EntityHandle createOceanRecorded(float level, const std::string& label, Finish&& finish)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::ocean::CreateOceanCommand create;
            create.config = watertool::creationDefaults(level);
            const services::EntityHandle created = dispatcher.execute(create);
            if (!created.isValid())
            {
                throw std::runtime_error("Creating the ocean failed (see logs_read)");
            }

            undo::UndoRecorder recorder(label);
            try
            {
                finish(created);
                recorder.add(undo::OceanLifetimeUndo::capture(undo::OceanLifetimeUndo::Kind::Created, created));
            }
            catch (...)
            {
                events::scene::DeleteEntityCommand remove;
                remove.entity = created;
                dispatcher.execute(remove);
                throw;
            }
            recorder.push();
            return created;
        }

        // ------------------------------------------------------------------
        // water_get
        // ------------------------------------------------------------------

        void registerWaterGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "water_get";
            tool.title = "Get water";
            tool.description =
                "Read the scene's water. 'ocean' (null when there is none) = {entity, name, visible, seaState (the current "
                "Beaufort number, read-only), params {level, weatherDriven, weatherResponse, bands {swell, agitation, "
                "ripples}, physics {physicsEnabled, drag, buoyancyStrength}}}. 'look' = the scene-wide water look the "
                "ocean holds and every lake and pool uses, in nine sections (color, refraction, caustics, shoreFoam, "
                "shoreline, reflections, absorption, antiTiling, interactiveRipples); lookSource is 'ocean', or "
                "'defaults' when there is no ocean (the look the renderer then uses). 'bodies' = the lakes and pools "
                "[{entity, name, type, center [x, z], halfExtents [hx, hz], level, depth, waves, physicsEnabled, "
                "active}]. 'notes' say what does not show and why. ocean.params, look and each body pass back unchanged "
                "to ocean_set_params, water_set_params and water_set_body (while a weather-driven ocean follows enabled "
                "weather its bands keep moving, so re-read them first). ranges:true adds every key's valid range. "
                "Works in Play mode too.";
            tool.inputSchema = closedObject({
                {"ranges", schema::boolean("Also return 'ranges': every key's valid values. Default false.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"ranges"}, "water_get");
                const ArgReader reader(args);
                const bool includeRanges = reader.optBool("ranges", false);

                const OceanRead ocean = readOcean();
                const bool visible = ocean.state.has_value() && ocean.state->isActive;
                std::vector<std::string> notes;
                if (ocean.state.has_value())
                {
                    if (!ocean.state->isActive)
                    {
                        notes.emplace_back(hiddenNote);
                    }
                    weatherNotes(*ocean.state, notes);
                }

                std::vector<services::WaterBodyEntry> entries;
                try
                {
                    entries = events::EventDispatcher::instance().query(events::ocean::GetWaterBodiesQuery{});
                }
                catch (const std::runtime_error&)
                {
                    // No ocean service: no bodies either.
                }
                std::sort(entries.begin(), entries.end(), [](const services::WaterBodyEntry& a, const services::WaterBodyEntry& b)
                {
                    return a.entity.id < b.entity.id;
                });

                const services::EntityHandle root = sceneRoot();
                nlohmann::json bodies = nlohmann::json::array();
                std::size_t activeBodies = 0;
                for (const services::WaterBodyEntry& entry : entries)
                {
                    const std::optional<services::EntityData> data = findEntity(entry.entity);
                    if (!data.has_value())
                    {
                        continue;
                    }
                    bodies.push_back(watertool::bodyJson(entry.entity, data->name, data->localTransform, entry.data));
                    activeBodies += entry.data.isActive ? 1u : 0u;
                    if (data->parent.has_value() && *data->parent != root)
                    {
                        notes.push_back("Body " + describe(*data) + " sits under entity " + entityName(*data->parent) +
                                        " (" + std::to_string(toId(*data->parent)) + "): bodies are placed by their LOCAL "
                                        "position and the parent's transform is ignored; keep them at the scene root.");
                    }
                    if (entry.data.bandMask != 0u && entry.data.isActive && !visible)
                    {
                        notes.push_back("Body " + describe(*data) + " has waves, but they are the ocean's FFT bands, which "
                                        "run only while the ocean is visible: it renders flat.");
                    }
                }
                if (activeBodies > water::MAX_WATER_BODY_CLIP_RECTS)
                {
                    notes.push_back("Only the " + std::to_string(water::MAX_WATER_BODY_CLIP_RECTS) +
                                    " bodies nearest the camera clip the ocean under them; the others still draw, but the "
                                    "sea shows through them where it is higher.");
                }
                if (!ocean.state.has_value() && entries.empty())
                {
                    notes.emplace_back("No water in the scene: ocean_enable {enabled:true} adds the sea, water_create_body "
                                       "a lake or pool.");
                }
                if (sceneLoadPending())
                {
                    notes.emplace_back("A scene load is pending: this still describes the old scene.");
                }

                nlohmann::json out{
                    {"ocean", ocean.state.has_value() ? watertool::oceanJson(ocean.entity, entityName(ocean.entity), *ocean.state)
                                                      : nlohmann::json(nullptr)},
                    {"look", watertool::lookJson(ocean.state.has_value() ? ocean.state->visual : watertool::Visual{})},
                    {"lookSource", ocean.state.has_value() ? "ocean" : "defaults"},
                    {"bodies", std::move(bodies)},
                    {"notes", notes}
                };
                if (includeRanges)
                {
                    out["ranges"] = watertool::rangesJson();
                }

                std::string summary = !ocean.state.has_value() ? std::string("No ocean")
                                    : visible                  ? "Ocean visible at level " + number(ocean.state->waterHeight)
                                                               : "Ocean hidden (level " + number(ocean.state->waterHeight) + ")";
                summary += ", " + std::to_string(entries.size()) + " water bod" + (entries.size() == 1 ? "y." : "ies.");
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // water_create_body
        // ------------------------------------------------------------------

        void registerWaterCreateBody(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "water_create_body";
            tool.title = "Create water body";
            tool.description =
                "Add a lake or pool: an axis-aligned rectangle of water at its own level (no rotation; there are no "
                "rivers - a long, narrow lake stands in for one). center [x, z] and halfExtents [hx, hz] are world "
                "metres (Y is up). The surface is 'level' (world metres) or 'levelAboveTerrain' metres above a terrain "
                "reference under the rectangle (terrainReference lowest | center | rim; rim with a small negative value, "
                "e.g. -0.3, fills a basin to just below its lip). Without 'depth' the water reaches 1 m below the lowest "
                "terrain point under it (10 m without terrain). 'waves' runs the ocean's FFT bands on it (swell, "
                "agitation, ripples; default none = mirror flat), which needs a visible ocean. A body takes the scene's "
                "water look (water_set_params); physicsEnabled floats dynamic rigid bodies with the drag and "
                "buoyancyStrength of ocean_set_params physics. Created at the scene root; remove it with entity_delete. "
                "One undo step. Refused in Play mode, while a scene load is pending and in World mode. Returns {entity, "
                "body, terrain, footprint {samples, valid, min, max, center, rimMin}, warnings}.";
            tool.inputSchema = closedObject(bodyProperties(true), {"center", "halfExtents"});
            tool.timeout = waterTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"center", "halfExtents", "level", "levelAboveTerrain", "terrainReference",
                                              "name", "type", "depth", "waves", "physicsEnabled", "active", "terrain"},
                                       "water_create_body");
                if (const std::optional<std::string> blocked = env::editBlocker("create a water body"))
                {
                    return ToolResult::error(*blocked);
                }
                if (isWorldModeActive())
                {
                    return ToolResult::error("Cannot create a water body in World mode: a body created here would not be "
                                             "assigned to a world sector (only entities created through the hierarchy "
                                             "are). Add water in an ordinary scene.");
                }

                const ArgReader reader(args);
                const glm::vec2 center = reader.requireGroundPoint("center");
                const glm::vec2 halfExtents = readHalfExtents(reader.raw("halfExtents"));
                const LevelRequest levelRequest = readLevel(reader, true);
                const watertool::TerrainReference reference = readReference(reader, levelRequest);
                const std::string type = reader.optEnum("type", typeNames(), "lake");
                const std::string name = reader.optString("name", typeIndex(type) == 1u ? "Pool" : "Lake");
                if (name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }
                std::optional<float> depth;
                if (reader.has("depth"))
                {
                    depth = static_cast<float>(reader.requireNumberInRange("depth", 0.0, watertool::maxBodyDepth));
                }
                const uint32_t bandMask = reader.has("waves") ? watertool::bandMaskFromNames(reader.raw("waves"), "waves")
                                                              : 0u;
                const bool physicsEnabled = reader.optBool("physicsEnabled", true);
                const bool active = reader.optBool("active", true);

                const std::optional<services::TerrainSummary> terrain =
                    samplingTerrain(reader, levelRequest.aboveTerrain.has_value());
                std::optional<watertool::FootprintStats> stats;
                if (terrain.has_value())
                {
                    stats = sampleFootprint(*terrain, center, halfExtents);
                }
                const float level = levelRequest.absolute.has_value()
                    ? *levelRequest.absolute
                    : relativeBodyLevel(stats, reference, *levelRequest.aboveTerrain);

                services::WaterBodyComponentData data;
                data.type = typeIndex(type);
                data.waterHeight = 0.0f;  // the transform carries the level
                data.halfExtents = halfExtents;
                data.depth = depth.value_or(defaultDepth(level, stats));
                data.bandMask = bandMask;
                data.physicsEnabled = physicsEnabled;
                data.isActive = active;
                const std::vector<std::string> warnings = bodyWarnings(level, stats, data, oceanVisible());

                auto& dispatcher = events::EventDispatcher::instance();
                events::ocean::CreateWaterBodyCommand command;
                command.data = data;
                command.position = glm::vec3(center.x, level, center.y);
                command.name = name;
                const services::EntityHandle created = dispatcher.execute(command);
                if (!created.isValid())
                {
                    return ToolResult::error("CreateWaterBody failed (see logs_read)");
                }

                undo::UndoRecorder recorder("MCP: Create water body (" + name + ")");
                try
                {
                    recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, created));
                }
                catch (...)
                {
                    // Not undoable: don't leave it behind.
                    events::scene::DeleteEntityCommand remove;
                    remove.entity = created;
                    dispatcher.execute(remove);
                    throw;
                }
                recorder.push();

                const std::optional<services::WaterBodyComponentData> stored = bodyData(created);
                nlohmann::json out{
                    {"entity", toId(created)},
                    {"body", watertool::bodyJson(created, name, requireTransform(created), stored.value_or(data))},
                    {"terrain", terrain.has_value() ? entityId(terrain->entity) : nlohmann::json(nullptr)},
                    {"footprint", stats.has_value() ? watertool::footprintJson(*stats) : nlohmann::json(nullptr)},
                    {"warnings", warnings}
                };
                std::string summary = "Created the " + type + " '" + name + "' (entity " + std::to_string(toId(created)) +
                                      "): " + number(2.0f * halfExtents.x) + " x " + number(2.0f * halfExtents.y) +
                                      " m at level " + number(level) + ", " + number(data.depth) + " m deep.";
                if (!warnings.empty())
                {
                    summary += " " + std::to_string(warnings.size()) + " warning(s).";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // water_set_body
        // ------------------------------------------------------------------

        void registerWaterSetBody(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "water_set_body";
            tool.title = "Set water body";
            tool.description =
                "Change a lake or pool (made by water_create_body or the editor): any of name, type, center, "
                "halfExtents, level or levelAboveTerrain (+ terrainReference, terrain), depth, waves, physicsEnabled, "
                "active. Only the given keys change, and a water_get body passes back unchanged (changed:false). center "
                "moves the entity; level moves it up or down (the component's own level offset is kept). active:false "
                "hides the body and stops it floating anything (entity_set_active does not hide water). One undo step, "
                "none when nothing changes. Refused in Play mode and while a scene load is pending. Returns {entity, "
                "changed, body, terrain, footprint, warnings}.";
            nlohmann::json properties = bodyProperties(false);
            properties["entity"] = schema::entity("Entity with a water body (water_get lists them)");
            tool.inputSchema = closedObject(std::move(properties), {"entity"});
            tool.timeout = waterTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "center", "halfExtents", "level", "levelAboveTerrain",
                                              "terrainReference", "name", "type", "depth", "waves", "physicsEnabled",
                                              "active", "terrain"},
                                       "water_set_body");
                if (const std::optional<std::string> blocked = env::editBlocker("change a water body"))
                {
                    return ToolResult::error(*blocked);
                }

                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const std::optional<services::WaterBodyComponentData> current = bodyData(entity.handle);
                if (!current.has_value())
                {
                    return ToolResult::error("Entity " + describe(entity) + " has no water body; water_create_body makes "
                                             "one (water_get lists the bodies)");
                }
                const services::TransformData transformBefore = requireTransform(entity.handle);

                services::WaterBodyComponentData data = *current;
                services::TransformData transform = transformBefore;
                if (reader.has("type"))
                {
                    data.type = typeIndex(reader.requireEnum("type", typeNames()));
                }
                if (reader.has("halfExtents"))
                {
                    data.halfExtents = readHalfExtents(reader.raw("halfExtents"));
                }
                if (reader.has("depth"))
                {
                    data.depth = static_cast<float>(reader.requireNumberInRange("depth", 0.0, watertool::maxBodyDepth));
                }
                if (reader.has("waves"))
                {
                    data.bandMask = watertool::bandMaskFromNames(reader.raw("waves"), "waves");
                }
                if (reader.has("physicsEnabled"))
                {
                    data.physicsEnabled = reader.requireBool("physicsEnabled");
                }
                if (reader.has("active"))
                {
                    data.isActive = reader.requireBool("active");
                }
                if (reader.has("center"))
                {
                    const glm::vec2 center = reader.requireGroundPoint("center");
                    transform.position.x = center.x;
                    transform.position.z = center.y;
                }
                std::string name = entity.name;
                if (reader.has("name"))
                {
                    name = reader.requireString("name");
                    if (name.empty())
                    {
                        throw ArgError("argument 'name' must not be empty");
                    }
                }
                const LevelRequest levelRequest = readLevel(reader, false);
                const watertool::TerrainReference reference = readReference(reader, levelRequest);

                const glm::vec2 center(transform.position.x, transform.position.z);
                const std::optional<services::TerrainSummary> terrain =
                    samplingTerrain(reader, levelRequest.aboveTerrain.has_value());
                std::optional<watertool::FootprintStats> stats;
                if (terrain.has_value())
                {
                    stats = sampleFootprint(*terrain, center, data.halfExtents);
                }
                std::optional<float> newLevel = levelRequest.absolute;
                if (levelRequest.aboveTerrain.has_value())
                {
                    newLevel = relativeBodyLevel(stats, reference, *levelRequest.aboveTerrain);
                }
                // Skipped when it is the level already reported, so an echoed body keeps its exact transform.
                if (newLevel.has_value() && *newLevel != watertool::bodyLevel(transform, data))
                {
                    transform.position.y = *newLevel - data.waterHeight;
                }
                const float level = watertool::bodyLevel(transform, data);
                const std::vector<std::string> warnings = bodyWarnings(level, stats, data, oceanVisible());

                const bool transformChanged = !(transform == transformBefore);
                const bool dataChanged = !watertool::sameBodyData(data, *current);
                const bool renamed = name != entity.name;
                const bool changed = transformChanged || dataChanged || renamed;
                if (changed)
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    const uint32_t id = toId(entity.handle);
                    undo::UndoRecorder recorder("MCP: Set water body (" + entity.name + ")");
                    try
                    {
                        if (transformChanged)
                        {
                            events::scene::SetTransformCommand command;
                            command.entity = entity.handle;
                            command.transform = transform;
                            dispatcher.execute(command);
                            recorder.add(std::make_unique<undo::TransformUndo>(id, entity.name, transformBefore, transform));
                        }
                        if (dataChanged)
                        {
                            events::ocean::SetWaterBodyDataCommand command;
                            command.entity = entity.handle;
                            command.data = data;
                            dispatcher.execute(command);
                            recorder.add(std::make_unique<undo::WaterBodyUndo>(id, entity.name, *current, data));
                        }
                        if (renamed)
                        {
                            events::scene::SetEntityNameCommand command;
                            command.entity = entity.handle;
                            command.newName = name;
                            dispatcher.execute(command);
                            recorder.add(std::make_unique<undo::RenameUndo>(id, entity.name, name));
                        }
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("The water body was not changed (rolled back): ") + e.what());
                    }
                    recorder.push();
                }

                const std::optional<services::WaterBodyComponentData> stored = bodyData(entity.handle);
                nlohmann::json out{
                    {"entity", toId(entity.handle)},
                    {"changed", changed},
                    {"body", watertool::bodyJson(entity.handle, name, requireTransform(entity.handle), stored.value_or(data))},
                    {"terrain", terrain.has_value() ? entityId(terrain->entity) : nlohmann::json(nullptr)},
                    {"footprint", stats.has_value() ? watertool::footprintJson(*stats) : nlohmann::json(nullptr)},
                    {"warnings", warnings}
                };
                std::string summary = changed ? "Updated the water body " + std::to_string(toId(entity.handle)) + " ('" +
                                                    name + "'): level " + number(level) + "."
                                              : "Unchanged: water body " + describe(entity) + " already has these values.";
                if (!warnings.empty())
                {
                    summary += " " + std::to_string(warnings.size()) + " warning(s).";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // water_set_params
        // ------------------------------------------------------------------

        void registerWaterSetParams(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "water_set_params";
            tool.title = "Set water look";
            tool.description =
                "Change the scene's water look, which the ocean holds and every lake and pool uses. Pass any of the "
                "sections, each a partial update: color {shallowColor (wave crests, rgb 0-1), deepColor (troughs; flat "
                "water shows their even mix), opacity, fresnelPower}; refraction; caustics (on the ground under the "
                "visible ocean only); shoreFoam (foam where water meets terrain or geometry, all water); shoreline "
                "(shoaling, breaking waves and wet sand: the visible ocean only, and it needs terrain); reflections "
                "(screen-space); absorption (Beer-Lambert, needs refractionStrength > 0); antiTiling (FFT waves); "
                "interactiveRipples (wakes and splashes, all water). water_get {ranges:true} lists every key and range; "
                "an unknown key, a wrong type or an out-of-range value fails the whole call and changes nothing. With no "
                "ocean in the scene the look is kept by a new HIDDEN ocean entity (createdHiddenOcean:true): no sea "
                "appears (ocean_enable {enabled:true} shows it). One undo step, none when nothing changes (changed:false). "
                "Refused in Play mode and while a scene load is pending. Returns {changed, createdHiddenOcean, ocean, "
                "diff, look, lookSource, warnings}.";
            nlohmann::json properties = nlohmann::json::object();
            for (const env::SectionTable<watertool::Visual>& section : watertool::lookSections())
            {
                properties[std::string(section.name)] =
                    env::schemaJson(section, "The " + std::string(section.name) + " look settings to change (partial).");
            }
            tool.inputSchema = closedObject(std::move(properties));
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"color", "refraction", "caustics", "shoreFoam", "shoreline", "reflections",
                                              "absorption", "antiTiling", "interactiveRipples"},
                                       "water_set_params");
                if (const std::optional<std::string> blocked = env::editBlocker("change the water look"))
                {
                    return ToolResult::error(*blocked);
                }

                const ArgReader reader(args);
                const OceanRead ocean = readOcean();
                const watertool::Visual base = ocean.state.has_value() ? ocean.state->visual : watertool::Visual{};
                watertool::Visual staged = base;
                bool any = false;
                for (const env::SectionTable<watertool::Visual>& section : watertool::lookSections())
                {
                    const std::string key(section.name);
                    if (reader.has(key.c_str()))
                    {
                        staged = env::strictPatch(section, staged, reader.raw(key.c_str()), key);
                        any = true;
                    }
                }
                if (!any)
                {
                    throw ArgError("nothing to change: pass at least one of color, refraction, caustics, shoreFoam, "
                                   "shoreline, reflections, absorption, antiTiling, interactiveRipples (water_get "
                                   "{ranges:true} lists their keys)");
                }

                nlohmann::json diff = nlohmann::json::object();
                for (const env::SectionTable<watertool::Visual>& section : watertool::lookSections())
                {
                    nlohmann::json sectionDiff = env::diffJson(section, base, staged);
                    if (!sectionDiff.empty())
                    {
                        diff[std::string(section.name)] = std::move(sectionDiff);
                    }
                }

                bool createdHidden = false;
                services::EntityHandle holder = ocean.entity;
                if (!diff.empty())
                {
                    try
                    {
                        if (!ocean.state.has_value())
                        {
                            holder = createOceanRecorded(0.0f, "MCP: Set water look (hidden ocean)",
                                                         [&staged](const services::EntityHandle& created)
                            {
                                std::optional<services::OceanStateData> hidden = watertool::readOceanState(created);
                                if (!hidden.has_value())
                                {
                                    throw std::runtime_error("the new ocean is not the scene's ocean");
                                }
                                hidden->isActive = false;
                                hidden->visual = staged;
                                watertool::writeOceanState(created, *hidden);
                            });
                            createdHidden = true;
                        }
                        else
                        {
                            services::OceanStateData after = *ocean.state;
                            after.visual = staged;
                            writeOceanRecorded(ocean.entity, *ocean.state, after, "MCP: Set water look");
                        }
                    }
                    catch (const std::exception& e)
                    {
                        return ToolResult::error(std::string("The water look was not changed: ") + e.what());
                    }
                }

                const bool visible = !createdHidden && ocean.state.has_value() && ocean.state->isActive;
                std::vector<std::string> warnings;
                if (createdHidden)
                {
                    warnings.emplace_back("The scene had no ocean, so a hidden ocean entity now holds the water look (lakes "
                                          "and pools use it); no sea shows until ocean_enable {enabled:true}.");
                }
                if (!visible && diff.contains("caustics"))
                {
                    warnings.emplace_back("caustics show only on the ground under the visible ocean (they come from its "
                                          "swell band), never under lakes or pools.");
                }
                if (diff.contains("shoreline"))
                {
                    if (!visible)
                    {
                        warnings.emplace_back("shoreline (shoaling, breaking waves, wet sand) applies to the visible ocean "
                                              "only, never to lakes or pools.");
                    }
                    if (!anyLiveTerrain())
                    {
                        warnings.emplace_back("shoreline effects need terrain under the water: the shore-depth field is "
                                              "baked from it.");
                    }
                }
                if (!visible && diff.contains("antiTiling"))
                {
                    warnings.emplace_back("antiTiling acts on the ocean's FFT waves, which run only while the ocean is "
                                          "visible.");
                }

                const std::optional<services::OceanStateData> now =
                    holder.isValid() ? watertool::readOceanState(holder) : std::optional<services::OceanStateData>();
                nlohmann::json out{
                    {"changed", !diff.empty()},
                    {"createdHiddenOcean", createdHidden},
                    {"ocean", now.has_value() ? entityId(holder) : nlohmann::json(nullptr)},
                    {"diff", diff},
                    {"look", watertool::lookJson(now.has_value() ? now->visual : staged)},
                    {"lookSource", now.has_value() ? "ocean" : "defaults"},
                    {"warnings", warnings}
                };
                std::string summary = diff.empty() ? std::string("Nothing changed.")
                                                   : "Updated the water look (" + std::to_string(diff.size()) + " section(s)).";
                if (createdHidden)
                {
                    summary += " Created a hidden ocean to hold it.";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ocean_enable
        // ------------------------------------------------------------------

        nlohmann::json oceanLevelProperty()
        {
            return schema::numberRange("Sea level in world metres", -watertool::maxLevel, watertool::maxLevel);
        }

        nlohmann::json oceanLevelAboveTerrainProperty()
        {
            return schema::numberRange("Sea level in metres above the terrain height at terrainPoint (default: the "
                                       "terrain's lowest point); negative = below it",
                                       -watertool::maxLevelAboveTerrain, watertool::maxLevelAboveTerrain);
        }

        nlohmann::json terrainPointProperty()
        {
            return schema::groundPoint("With levelAboveTerrain: the [x, z] whose terrain height the level is measured "
                                       "from. Default: the terrain's lowest point (sampled on a 32 x 32 grid).");
        }

        nlohmann::json seaStateProperty()
        {
            return schema::numberRange("Sea state as a Beaufort number: 0 mirror calm, 3 gentle, 5 fresh, 7 rough, 9 very "
                                       "rough, 12 hurricane. Sets every band's wind, amplitude, choppiness and foam.",
                                       0.0, watertool::maxBeaufort);
        }

        void registerOceanEnable(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ocean_enable";
            tool.title = "Enable / hide ocean";
            tool.description =
                "Show, hide, create or remove the scene's ocean: the endless FFT sea at one level. In the editor it is a "
                "camera-centred grid about 1.8 km across, so it also shows past the terrain's edges; every lake and pool "
                "suppresses it inside its own rectangle. enabled:true shows it, creating it when there is none (the "
                "Ocean Editor's defaults), optionally at 'level' or 'levelAboveTerrain' metres above the terrain height "
                "at terrainPoint [x, z] (default: the terrain's lowest point), and at 'seaState' (Beaufort 0-12). "
                "enabled:false HIDES it and keeps every setting: lakes and pools keep its look, but there is no sea "
                "surface, no waves (bodies render flat), no ocean buoyancy and no underwater effect. remove:true (with "
                "enabled:false) deletes the Ocean entity - refused while it has children or other components; lakes then "
                "use the default look. One undo step, none when nothing changes. Refused in Play mode and while a scene "
                "load is pending. Returns {changed, created, removed, ocean, terrainReference, warnings, notes}.";
            tool.inputSchema = closedObject({
                {"enabled", schema::boolean("true = show the ocean (create it when missing), false = hide it")},
                {"remove", schema::boolean("With enabled:false: delete the Ocean entity instead of hiding it. Default false.")},
                {"level", oceanLevelProperty()},
                {"levelAboveTerrain", oceanLevelAboveTerrainProperty()},
                {"terrainPoint", terrainPointProperty()},
                {"terrain", schema::entity("With levelAboveTerrain: the terrain to measure. Default: the scene's only terrain.")},
                {"seaState", seaStateProperty()}
            }, {"enabled"});
            tool.timeout = waterTimeout;
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"enabled", "remove", "level", "levelAboveTerrain", "terrainPoint", "terrain",
                                              "seaState"},
                                       "ocean_enable");
                if (const std::optional<std::string> blocked = env::editBlocker("change the ocean"))
                {
                    return ToolResult::error(*blocked);
                }

                const ArgReader reader(args);
                const bool enabled = reader.requireBool("enabled");
                const bool remove = reader.optBool("remove", false);
                const LevelRequest levelRequest = readLevel(reader, false);
                std::optional<float> seaState;
                if (reader.has("seaState"))
                {
                    seaState = static_cast<float>(reader.requireNumberInRange("seaState", 0.0, watertool::maxBeaufort));
                }
                if (remove && enabled)
                {
                    throw ArgError("'remove' needs enabled:false (it deletes the ocean entity)");
                }
                if (!enabled && (levelRequest.absolute.has_value() || levelRequest.aboveTerrain.has_value() ||
                                 seaState.has_value() || reader.has("terrainPoint") || reader.has("terrain")))
                {
                    throw ArgError("with enabled:false only 'remove' applies: set the level and the sea state with "
                                   "ocean_set_params (a hidden ocean keeps them)");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                const OceanRead ocean = readOcean();
                std::vector<std::string> warnings;
                std::vector<std::string> notes;
                std::string nothingReason;
                nlohmann::json terrainReference = nullptr;
                bool changed = false;
                bool created = false;
                bool removed = false;
                services::EntityHandle oceanEntity = ocean.entity;

                if (enabled)
                {
                    const std::optional<float> requested = requestedOceanLevel(reader, levelRequest, terrainReference);
                    if (!ocean.state.has_value())
                    {
                        try
                        {
                            oceanEntity = createOceanRecorded(requested.value_or(0.0f), "MCP: Create ocean",
                                                              [&seaState](const services::EntityHandle& handle)
                            {
                                if (!seaState.has_value())
                                {
                                    return;
                                }
                                std::optional<services::OceanStateData> state = watertool::readOceanState(handle);
                                if (!state.has_value())
                                {
                                    throw std::runtime_error("the new ocean is not the scene's ocean");
                                }
                                watertool::applySeaState(*state, *seaState);
                                watertool::writeOceanState(handle, *state);
                            });
                        }
                        catch (const std::exception& e)
                        {
                            return ToolResult::error(std::string("The ocean was not created: ") + e.what());
                        }
                        changed = true;
                        created = true;
                    }
                    else
                    {
                        services::OceanStateData after = *ocean.state;
                        after.isActive = true;
                        if (requested.has_value())
                        {
                            after.waterHeight = *requested;
                        }
                        if (seaState.has_value())
                        {
                            if (after.weatherDriven)
                            {
                                throw ArgError("'seaState' is refused while weatherDriven is on (the live weather sets the "
                                               "sea state): ocean_set_params {weatherDriven:false, seaState} authors it");
                            }
                            watertool::applySeaState(after, *seaState);
                        }
                        try
                        {
                            changed = writeOceanRecorded(ocean.entity, *ocean.state, after,
                                                         ocean.state->isActive ? "MCP: Set ocean" : "MCP: Show ocean");
                        }
                        catch (const std::exception& e)
                        {
                            return ToolResult::error(std::string("The ocean was not changed: ") + e.what());
                        }
                    }
                }
                else if (!ocean.state.has_value())
                {
                    nothingReason = remove ? "there is no ocean to remove." : "there is no ocean to hide.";
                    notes.push_back("Nothing changed: " + nothingReason);
                }
                else if (!remove)
                {
                    services::OceanStateData after = *ocean.state;
                    after.isActive = false;
                    try
                    {
                        changed = writeOceanRecorded(ocean.entity, *ocean.state, after, "MCP: Hide ocean");
                    }
                    catch (const std::exception& e)
                    {
                        return ToolResult::error(std::string("The ocean was not hidden: ") + e.what());
                    }
                }
                else
                {
                    const std::optional<services::EntityData> data = findEntity(ocean.entity);
                    if (!data.has_value())
                    {
                        return ToolResult::error("The ocean entity " + std::to_string(toId(ocean.entity)) +
                                                 " does not exist");
                    }
                    const watertool::OceanExtras extras = watertool::oceanExtras(ocean.entity);
                    if (extras.children > 0)
                    {
                        return ToolResult::error("The Ocean entity " + describe(*data) + " has " +
                                                 std::to_string(extras.children) + " child entit" +
                                                 (extras.children == 1 ? "y" : "ies") +
                                                 ": removing it would delete them too, and its undo could not bring them "
                                                 "back. Move or delete them first (entity_set_parent / entity_delete), or "
                                                 "hide the ocean with enabled:false.");
                    }
                    if (!extras.components.empty())
                    {
                        return ToolResult::error("The Ocean entity " + describe(*data) + " also carries " +
                                                 env::detail::joined(extras.components) + ": removing it would lose them, "
                                                 "and its undo could not bring them back. Remove those components first "
                                                 "(component_remove), or hide the ocean with enabled:false.");
                    }

                    std::unique_ptr<undo::OceanLifetimeUndo> record =
                        undo::OceanLifetimeUndo::capture(undo::OceanLifetimeUndo::Kind::Deleted, ocean.entity);
                    events::scene::DeleteEntityCommand command;
                    command.entity = ocean.entity;
                    if (!dispatcher.execute(command))
                    {
                        return ToolResult::error("Deleting the ocean entity " + describe(*data) + " failed");
                    }
                    undo::UndoRecorder recorder("MCP: Remove ocean (" + data->name + ")");
                    recorder.add(std::move(record));
                    recorder.push();
                    changed = true;
                    removed = true;
                    oceanEntity = {};

                    std::vector<services::WaterBodyEntry> bodies;
                    try
                    {
                        bodies = dispatcher.query(events::ocean::GetWaterBodiesQuery{});
                    }
                    catch (const std::runtime_error&)
                    {
                    }
                    if (!bodies.empty())
                    {
                        warnings.emplace_back("Lakes and pools now use the default water look; water_set_params keeps a "
                                              "custom one in a new hidden ocean.");
                    }
                }

                const std::optional<services::OceanStateData> now =
                    oceanEntity.isValid() ? watertool::readOceanState(oceanEntity) : std::optional<services::OceanStateData>();
                if (now.has_value())
                {
                    if (!now->isActive)
                    {
                        notes.emplace_back(hiddenNote);
                    }
                    weatherNotes(*now, notes);
                }
                nlohmann::json out{
                    {"changed", changed},
                    {"created", created},
                    {"removed", removed},
                    {"ocean", now.has_value() ? watertool::oceanJson(oceanEntity, entityName(oceanEntity), *now)
                                              : nlohmann::json(nullptr)},
                    {"terrainReference", terrainReference},
                    {"warnings", warnings},
                    {"notes", notes}
                };
                std::string summary;
                if (created)
                {
                    summary = "Created the ocean (entity " + std::to_string(toId(oceanEntity)) + ") at level " +
                              number(now.has_value() ? now->waterHeight : 0.0f) + ".";
                }
                else if (removed)
                {
                    summary = "Removed the ocean.";
                }
                else if (!changed)
                {
                    summary = nothingReason.empty() ? std::string("Nothing changed.") : "Nothing changed: " + nothingReason;
                }
                else
                {
                    summary = now.has_value() && now->isActive ? "The ocean is visible at level " + number(now->waterHeight) + "."
                                                               : std::string("The ocean is hidden (its settings are kept).");
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ocean_set_params
        // ------------------------------------------------------------------

        void registerOceanSetParams(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ocean_set_params";
            tool.title = "Set ocean parameters";
            tool.description =
                "Tune the ocean, visible or hidden: level or levelAboveTerrain (+ terrainPoint, terrain); seaState "
                "(Beaufort 0-12: sets every band's wind, amplitude, choppiness and foam from one number) or windSpeed "
                "(m/s, mapped to a Beaufort number the same way); windDirection (degrees; the bands keep their offsets); "
                "choppiness (every band); bands {swell, agitation, ripples: any of resolution 64 / 128 / 256 / 512, "
                "patchSize, windSpeed, windDirection, amplitude, choppiness, foamThreshold, displacementScale, enabled, "
                "foamPersistence, foamDecay}; weatherDriven / weatherResponse (the live weather drives the sea state while "
                "weather is enabled); physics {physicsEnabled (the ocean floats dynamic rigid bodies), drag, "
                "buoyancyStrength} - drag and buoyancyStrength apply to EVERY water, lakes and pools included. Applied in "
                "that order (weather, level, sea state, wind direction, choppiness, bands, physics), so explicit band keys "
                "win. While weatherDriven is on, the weather owns the bands' windSpeed, windDirection, amplitude, "
                "choppiness, displacementScale and foamThreshold and the sea state: changing them is refused unless the "
                "same call passes weatherDriven:false. water_get's ocean.params passes back unchanged (changed:false). "
                "One undo step, none when nothing changes. Refused in Play mode and while a scene load is pending. "
                "Returns {changed, ocean, terrainReference, warnings, notes}.";
            nlohmann::json bands = nlohmann::json::object();
            bands["swell"] = env::schemaJson(watertool::bandTable(), "Swell: the long waves (partial).");
            bands["agitation"] = env::schemaJson(watertool::bandTable(), "Agitation: mid-size wind waves (partial).");
            bands["ripples"] = env::schemaJson(watertool::bandTable(), "Ripples: fine surface detail (partial).");
            nlohmann::json bandsSchema = closedObject(std::move(bands));
            bandsSchema["description"] = "Per-band FFT parameters (partial).";
            tool.inputSchema = closedObject({
                {"level", oceanLevelProperty()},
                {"levelAboveTerrain", oceanLevelAboveTerrainProperty()},
                {"terrainPoint", terrainPointProperty()},
                {"terrain", schema::entity("With levelAboveTerrain: the terrain to measure. Default: the scene's only terrain.")},
                {"seaState", seaStateProperty()},
                {"windSpeed", schema::numberRange("Wind speed in m/s, mapped to a Beaufort sea state (0 - 34.75)", 0.0,
                                                  watertool::maxSeaStateWindSpeed())},
                {"windDirection", schema::numberRange("Wind direction in degrees for the swell band; the other bands keep "
                                                      "their offsets", -watertool::maxWindDirection,
                                                      watertool::maxWindDirection)},
                {"choppiness", schema::numberRange("Choppiness of every band (sharper crests)", 0.0, watertool::maxChoppiness)},
                {"bands", std::move(bandsSchema)},
                {"weatherDriven", schema::boolean("The live weather drives the sea state while weather is enabled")},
                {"weatherResponse", schema::numberRange("How strongly the ocean follows the weather (1 = directly)", 0.0,
                                                        watertool::maxWeatherResponse)},
                {"physics", env::schemaJson(watertool::physicsTable(), "Ocean physics (partial).")}
            });
            tool.timeout = waterTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"level", "levelAboveTerrain", "terrainPoint", "terrain", "seaState",
                                              "windSpeed", "windDirection", "choppiness", "bands", "weatherDriven",
                                              "weatherResponse", "physics"},
                                       "ocean_set_params");
                if (const std::optional<std::string> blocked = env::editBlocker("change the ocean"))
                {
                    return ToolResult::error(*blocked);
                }

                const ArgReader reader(args);
                bool any = false;
                for (const char* key : {"level", "levelAboveTerrain", "seaState", "windSpeed", "windDirection",
                                        "choppiness", "bands", "weatherDriven", "weatherResponse", "physics"})
                {
                    any = any || reader.has(key);
                }
                if (!any)
                {
                    throw ArgError("nothing to change: pass at least one of level, levelAboveTerrain, seaState, windSpeed, "
                                   "windDirection, choppiness, bands, weatherDriven, weatherResponse, physics");
                }

                const OceanRead ocean = readOcean();
                if (!ocean.state.has_value())
                {
                    return ToolResult::error("There is no ocean in the scene: ocean_enable {enabled:true} creates one");
                }
                const services::OceanStateData& before = *ocean.state;

                // Every argument is checked before anything changes.
                const LevelRequest levelRequest = readLevel(reader, false);
                if (reader.has("seaState") && reader.has("windSpeed"))
                {
                    throw ArgError("pass either 'seaState' (Beaufort 0-12) or 'windSpeed' (m/s), not both");
                }
                std::optional<float> beaufort;
                if (reader.has("seaState"))
                {
                    beaufort = static_cast<float>(reader.requireNumberInRange("seaState", 0.0, watertool::maxBeaufort));
                }
                if (reader.has("windSpeed"))
                {
                    const double speed = reader.requireNumberInRange("windSpeed", 0.0, watertool::maxSeaStateWindSpeed());
                    beaufort = water::beaufortFromWindSpeed(static_cast<float>(speed));
                }
                std::optional<float> windDirection;
                if (reader.has("windDirection"))
                {
                    windDirection = static_cast<float>(reader.requireNumberInRange(
                        "windDirection", -watertool::maxWindDirection, watertool::maxWindDirection));
                }
                std::optional<float> choppiness;
                if (reader.has("choppiness"))
                {
                    choppiness = static_cast<float>(reader.requireNumberInRange("choppiness", 0.0, watertool::maxChoppiness));
                }
                std::optional<bool> weatherDriven;
                if (reader.has("weatherDriven"))
                {
                    weatherDriven = reader.requireBool("weatherDriven");
                }
                std::optional<float> weatherResponse;
                if (reader.has("weatherResponse"))
                {
                    weatherResponse = static_cast<float>(
                        reader.requireNumberInRange("weatherResponse", 0.0, watertool::maxWeatherResponse));
                }
                const nlohmann::json* bands = reader.has("bands") ? &reader.raw("bands") : nullptr;
                if (bands != nullptr)
                {
                    env::rejectUnknownKeys(*bands, {"swell", "agitation", "ripples"}, "bands");
                    for (auto it = bands->begin(); it != bands->end(); ++it)
                    {
                        const int index = watertool::bandIndex(it.key());
                        env::strictPatch(watertool::bandTable(), before.bands[index], it.value(), "bands." + it.key());
                    }
                }
                const nlohmann::json* physics = reader.has("physics") ? &reader.raw("physics") : nullptr;
                if (physics != nullptr)
                {
                    if (physics->is_object() && physics->contains("density"))
                    {
                        throw ArgError("'physics.density' is not exposed: buoyancy never reads it (drag and "
                                       "buoyancyStrength set how the water floats things)");
                    }
                    env::strictPatch(watertool::physicsTable(), before.physics, *physics, "physics");
                }

                nlohmann::json terrainReference = nullptr;
                const std::optional<float> level = requestedOceanLevel(reader, levelRequest, terrainReference);

                services::OceanStateData after = before;
                if (weatherDriven.has_value())
                {
                    after.weatherDriven = *weatherDriven;
                }
                if (weatherResponse.has_value())
                {
                    after.weatherResponse = *weatherResponse;
                }
                if (level.has_value())
                {
                    after.waterHeight = *level;
                }
                if (beaufort.has_value())
                {
                    watertool::applySeaState(after, *beaufort);
                }
                if (windDirection.has_value())
                {
                    watertool::rotateWind(after, *windDirection);
                }
                if (choppiness.has_value())
                {
                    watertool::setChoppiness(after, *choppiness);
                }
                if (bands != nullptr)
                {
                    for (auto it = bands->begin(); it != bands->end(); ++it)
                    {
                        const int index = watertool::bandIndex(it.key());
                        after.bands[index] =
                            env::strictPatch(watertool::bandTable(), after.bands[index], it.value(), "bands." + it.key());
                    }
                }
                if (physics != nullptr)
                {
                    after.physics = env::strictPatch(watertool::physicsTable(), after.physics, *physics, "physics");
                }
                if (after.weatherDriven && watertool::weatherOwnedChanged(before, after))
                {
                    throw ArgError("weatherDriven is on: the live weather owns every band's windSpeed, windDirection, "
                                   "amplitude, choppiness, displacementScale and foamThreshold and the sea state. Pass "
                                   "weatherDriven:false in the same call to author them.");
                }

                bool changed = false;
                try
                {
                    changed = writeOceanRecorded(ocean.entity, before, after, "MCP: Set ocean");
                }
                catch (const std::exception& e)
                {
                    return ToolResult::error(std::string("The ocean was not changed: ") + e.what());
                }

                std::vector<std::string> notes;
                const std::optional<services::OceanStateData> now = watertool::readOceanState(ocean.entity);
                if (now.has_value())
                {
                    if (!now->isActive)
                    {
                        notes.emplace_back("The ocean is hidden: these settings show once ocean_enable {enabled:true}.");
                    }
                    weatherNotes(*now, notes);
                }
                nlohmann::json out{
                    {"changed", changed},
                    {"ocean", now.has_value() ? watertool::oceanJson(ocean.entity, entityName(ocean.entity), *now)
                                              : nlohmann::json(nullptr)},
                    {"terrainReference", terrainReference},
                    {"warnings", nlohmann::json::array()},
                    {"notes", notes}
                };
                const std::string summary = changed
                    ? "Updated the ocean (" + watertool::partsText(watertool::changedParts(before, after)) + ")."
                    : std::string("Nothing changed.");
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerWaterTools(ToolRegistry& registry, const ToolContext&)
    {
        registerWaterGet(registry);
        registerWaterCreateBody(registry);
        registerWaterSetBody(registry);
        registerWaterSetParams(registry);
        registerOceanEnable(registry);
        registerOceanSetParams(registry);
    }
}
