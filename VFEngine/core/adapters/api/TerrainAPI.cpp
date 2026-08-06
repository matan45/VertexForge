// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "TerrainAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/terrain/TerrainEvents.hpp"
#include "../../../services/events/terrain/TerrainRuntimeEditEvents.hpp"
#include "terrain/TerrainHeightAtResult.hpp"

#include <algorithm>
#include <cmath>

namespace core::api
{
    namespace
    {
        // Every mutating native returns a tile count, and 0 is the universal "nothing happened":
        // rejected arguments, off the loaded terrain, tile not resident, budget exhausted, or a
        // terrain save in flight. Scripts branch on that one value instead of an ok/count pair.
        value::Value noTilesTouched()
        {
            return value::Value(static_cast<int64_t>(0));
        }

        // extractInt64 accepts only ValueType::INT and returns -1 on any mismatch, which is a
        // perfectly plausible enum selector. extractFloat accepts INT *and* FLOAT (NativeHelpers),
        // so rounding it is the robust way to read a script-supplied index.
        int32_t extractIndex(const value::Value& v)
        {
            const float raw = extractFloat(v);
            if (!std::isfinite(raw))
                return -1;
            return static_cast<int32_t>(std::lround(raw));
        }

        template <typename TEnum>
        TEnum extractEnum(const value::Value& v, TEnum fallback, int32_t maxValue)
        {
            const int32_t index = extractIndex(v);
            if (index < 0 || index > maxValue)
                return fallback;
            return static_cast<TEnum>(index);
        }

        bool readBrushCenter(std::span<const value::Value> args, glm::vec2& out)
        {
            out = glm::vec2(extractFloat(args[0]), extractFloat(args[1]));
            return std::isfinite(out.x) && std::isfinite(out.y);
        }

        // Returns 0 when the radius is unusable, so the caller can bail with the sentinel.
        float readBrushRadius(const value::Value& v, float maxRadius)
        {
            const float radius = extractFloat(v);
            if (!std::isfinite(radius) || radius <= 0.0f)
                return 0.0f;
            return std::min(radius, maxRadius);
        }
    }

    void TerrainAPI::beginFrame()
    {
        editCountThisFrame = 0;
    }

    bool TerrainAPI::consumeEditBudget(const char* nativeName)
    {
        if (editCountThisFrame >= MAX_EDITS_PER_FRAME)
        {
            // Warn only on the call that crosses the line, not on every one after it.
            if (editCountThisFrame == MAX_EDITS_PER_FRAME)
            {
                vfLogWarning("[Script] Terrain edit rate limit exceeded ({}/frame); {} ignored",
                             MAX_EDITS_PER_FRAME, nativeName);
                ++editCountThisFrame;
            }
            return false;
        }

        ++editCountThisFrame;
        return true;
    }

    void TerrainAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // _native_terrain_getHeightAt(worldX, worldZ) -> float[2] { valid(0/1), height }
        // Reads the CPU heightfield directly (no physics) so it is reliable at runtime.
        interpreter->registerNativeFunction("_native_terrain_getHeightAt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto result = std::make_shared<value::NativeArray>(2, value::ValueType::FLOAT);
                if (args.size() < 2)
                {
                    result->set(0, value::Value(0.0f));
                    result->set(1, value::Value(0.0f));
                    return value::Value(result);
                }

                events::terrain::GetTerrainHeightAtQuery query;
                query.worldX = extractFloat(args[0]);
                query.worldZ = extractFloat(args[1]);
                terrain::TerrainHeightAtResult hit = dispatcher.query(query);

                result->set(0, value::Value(hit.valid ? 1.0f : 0.0f));
                result->set(1, value::Value(hit.height));
                return value::Value(result);
            }});

        // VK-1624 runtime editing. Each of the three mutators returns the number of tiles it
        // actually changed. Every dispatch is wrapped: the terrain command handlers are registered
        // by TerrainService, and a scene with no terrain service at all would otherwise let
        // EventDispatcher's "no handler registered" exception unwind through the script VM.

        // _native_terrain_deform(cx, cz, radius, amount [, mode, falloff, shape]) -> int tilesTouched
        // `amount` is world-Y metres: a delta for MODE_ADD, an absolute target for MODE_SET.
        interpreter->registerNativeFunction("_native_terrain_deform",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4)
                    return noTilesTouched();

                events::terrainEdit::DeformTerrainCommand cmd;
                if (!readBrushCenter(args, cmd.worldPosition))
                    return noTilesTouched();

                cmd.radius = readBrushRadius(args[2], MAX_BRUSH_RADIUS);
                if (cmd.radius <= 0.0f)
                    return noTilesTouched();

                cmd.amount = extractFloat(args[3]);
                if (!std::isfinite(cmd.amount))
                    return noTilesTouched();

                if (args.size() > 4)
                    cmd.mode = extractEnum(args[4], ::terrain::HeightEditMode::Add, 1);
                if (args.size() > 5)
                    cmd.falloff = extractEnum(args[5], ::terrain::BrushFalloff::Smooth, 3);
                if (args.size() > 6)
                    cmd.shape = extractEnum(args[6], ::terrain::BrushShape::Circle, 1);

                if (!consumeEditBudget("Terrain.deform"))
                    return noTilesTouched();

                try
                {
                    return value::Value(static_cast<int64_t>(
                        events::EventDispatcher::instance().execute(cmd)));
                }
                catch (const std::exception&)
                {
                    return noTilesTouched();
                }
            }});

        // _native_terrain_paint(cx, cz, radius, layer [, strength, opacity, falloff, shape, mode])
        //   -> int tilesTouched
        // `layer` is a palette layer (0-31), not a weight channel. A tile whose eight channels are
        // all in use by other layers is skipped rather than having one evicted.
        interpreter->registerNativeFunction("_native_terrain_paint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4)
                    return noTilesTouched();

                events::terrainEdit::PaintTerrainLayerCommand cmd;
                if (!readBrushCenter(args, cmd.worldPosition))
                    return noTilesTouched();

                cmd.radius = readBrushRadius(args[2], MAX_BRUSH_RADIUS);
                if (cmd.radius <= 0.0f)
                    return noTilesTouched();

                const int32_t layer = extractIndex(args[3]);
                if (layer < 0)
                    return noTilesTouched();
                cmd.layerIndex = static_cast<uint32_t>(layer);

                if (args.size() > 4)
                    cmd.strength = extractFloat(args[4]);
                if (args.size() > 5)
                    cmd.opacity = extractFloat(args[5]);
                if (args.size() > 6)
                    cmd.falloff = extractEnum(args[6], ::terrain::BrushFalloff::Smooth, 3);
                if (args.size() > 7)
                    cmd.shape = extractEnum(args[7], ::terrain::BrushShape::Circle, 1);
                if (args.size() > 8)
                {
                    // 0-3 only. SetBaseLayer (4) is out of range here and the service rejects it
                    // anyway: it wipes every weight channel of each touched tile.
                    cmd.paintMode = extractEnum(args[8], ::terrain::PaintBrushType::PaintLayer, 3);
                }

                if (!std::isfinite(cmd.strength) || !std::isfinite(cmd.opacity))
                    return noTilesTouched();

                if (!consumeEditBudget("Terrain.paint"))
                    return noTilesTouched();

                try
                {
                    return value::Value(static_cast<int64_t>(
                        events::EventDispatcher::instance().execute(cmd)));
                }
                catch (const std::exception&)
                {
                    return noTilesTouched();
                }
            }});

        // _native_terrain_setHole(cx, cz, radius, makeHole [, shape]) -> int tilesTouched
        // Holes are per-quad, so the effective edge snaps to the quad grid.
        interpreter->registerNativeFunction("_native_terrain_setHole",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4)
                    return noTilesTouched();

                events::terrainEdit::SetTerrainHolesCommand cmd;
                if (!readBrushCenter(args, cmd.worldPosition))
                    return noTilesTouched();

                cmd.radius = readBrushRadius(args[2], MAX_BRUSH_RADIUS);
                if (cmd.radius <= 0.0f)
                    return noTilesTouched();

                cmd.makeHole = extractBool(args[3]);
                if (args.size() > 4)
                    cmd.shape = extractEnum(args[4], ::terrain::BrushShape::Circle, 1);

                if (!consumeEditBudget("Terrain.setHole"))
                    return noTilesTouched();

                try
                {
                    return value::Value(static_cast<int64_t>(
                        events::EventDispatcher::instance().execute(cmd)));
                }
                catch (const std::exception&)
                {
                    return noTilesTouched();
                }
            }});

        // _native_terrain_beginBatch() -> void
        // Holds the seam weld and the collider rebuild until the matching flush, so a salvo of
        // edits pays for them once. Not required for correctness -- an unbatched edit is drained on
        // the next terrain tick regardless.
        interpreter->registerNativeFunction("_native_terrain_beginBatch",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                try
                {
                    events::EventDispatcher::instance().execute(
                        events::terrainEdit::BeginTerrainEditBatchCommand{});
                }
                catch (const std::exception&)
                {
                    // No terrain service in this scene; nothing to batch.
                }
                return value::Value(std::monostate{});
            }});

        // _native_terrain_flush() -> int tilesPending
        // Closes the batch. The work itself happens on the next terrain tick, which is where the
        // camera position the async collider path needs actually exists.
        interpreter->registerNativeFunction("_native_terrain_flush",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                try
                {
                    return value::Value(static_cast<int64_t>(
                        events::EventDispatcher::instance().execute(
                            events::terrainEdit::FlushTerrainEditsCommand{})));
                }
                catch (const std::exception&)
                {
                    return noTilesTouched();
                }
            }});

        vfLogInfo("[TerrainAPI] Registered Terrain native functions");
    }
}
