// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "TerrainAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/terrain/TerrainEvents.hpp"
#include "terrain/TerrainHeightAtResult.hpp"

namespace core::api
{
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

        vfLogInfo("[TerrainAPI] Registered Terrain native functions");
    }
}
