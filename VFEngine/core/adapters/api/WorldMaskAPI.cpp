// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "WorldMaskAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/PluginTextureEvents.hpp"

namespace core::api
{
    void WorldMaskAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // _native_worldMask_sample(worldX, worldZ) -> float in [0,1]
        // CPU readback of the renderer's bound world-space mask (e.g. fog of war).
        // Returns 1.0 when no mask is bound / disabled / out of bounds, matching the
        // shader's "unaffected" semantics — so callers see no effect when fog is off.
        interpreter->registerNativeFunction("_native_worldMask_sample",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(1.0f);

                events::plugintexture::SampleWorldMaskQuery query;
                query.worldX = extractFloat(args[0]);
                query.worldZ = extractFloat(args[1]);
                try
                {
                    return value::Value(events::EventDispatcher::instance().query(query));
                }
                catch (const std::exception&)
                {
                    // No plugin-texture handler registered (e.g. mask system unused) —
                    // degrade to the shader's "unaffected" value rather than throwing
                    // out of the script VM.
                    return value::Value(1.0f);
                }
            }});

        vfLogInfo("[WorldMaskAPI] Registered WorldMask native functions");
    }
}
