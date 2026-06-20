// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "RuntimePickerAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/input/RuntimePickerEvents.hpp"
#include "../../../services/events/physics/PhysicsSettingsEvents.hpp"
#include "../../../services/events/terrain/TerrainEvents.hpp"

#include <glm/glm.hpp>
#include <string>

namespace core::api
{
    namespace
    {
        // Resolve comma-separated layer names (e.g. "Dynamic,Sensor") to a uint16_t bitmask.
        // Returns 0xFFFF (all layers) if the string is empty. Mirrors PhysicsAPI's resolver.
        uint16_t resolveLayerMask(const std::string& layerNames)
        {
            if (layerNames.empty()) return 0xFFFF;

            auto& dispatcher = events::EventDispatcher::instance();
            events::physics::GetPhysicsSettingsQuery settingsQuery;
            auto settings = dispatcher.query(settingsQuery);

            uint16_t mask = 0;
            size_t start = 0;
            while (start < layerNames.size())
            {
                size_t end = layerNames.find(',', start);
                if (end == std::string::npos) end = layerNames.size();

                size_t nameStart = start;
                size_t nameEnd = end;
                while (nameStart < nameEnd && layerNames[nameStart] == ' ') ++nameStart;
                while (nameEnd > nameStart && layerNames[nameEnd - 1] == ' ') --nameEnd;

                std::string name = layerNames.substr(nameStart, nameEnd - nameStart);
                if (!name.empty())
                {
                    const auto* layer = settings.getLayerByName(name);
                    if (layer)
                        mask |= (1u << layer->index);
                    else
                        vfLogWarning("[Script] Picker: unknown collision layer name: '{}'", name);
                }
                start = end + 1;
            }

            return mask == 0 ? 0xFFFF : mask;
        }

        value::Value missArray()
        {
            auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
            result->set(0, value::Value(0.0f));
            return value::Value(result);
        }
    }

    void RuntimePickerAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // _native_picker_screenToWorldRay(screenX, screenY)
        //   miss -> [0.0]; hit -> [1.0, ox, oy, oz, dx, dy, dz]
        interpreter->registerNativeFunction("_native_picker_screenToWorldRay",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return missArray();

                events::input::ScreenToWorldRayQuery q;
                q.screenPos = glm::vec2(extractFloat(args[0]), extractFloat(args[1]));
                auto ray = dispatcher.query(q);
                if (!ray.has_value()) return missArray();

                auto result = std::make_shared<value::NativeArray>(7, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(ray->origin.x));
                result->set(2, value::Value(ray->origin.y));
                result->set(3, value::Value(ray->origin.z));
                result->set(4, value::Value(ray->direction.x));
                result->set(5, value::Value(ray->direction.y));
                result->set(6, value::Value(ray->direction.z));
                return value::Value(result);
            }});

        // _native_picker_worldToScreen(worldX, worldY, worldZ)
        //   miss (no camera / behind camera) -> [0.0]; hit -> [1.0, screenX, screenY]
        //   Inverse of screenToWorldRay; coordinates may lie outside the viewport
        //   for in-front-but-offscreen points.
        interpreter->registerNativeFunction("_native_picker_worldToScreen",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 3) return missArray();

                events::input::WorldToScreenQuery q;
                q.worldPos = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2]));
                auto screen = dispatcher.query(q);
                if (!screen.has_value()) return missArray();

                auto result = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(screen->x));
                result->set(2, value::Value(screen->y));
                return value::Value(result);
            }});

        // _native_picker_pickEntity(screenX, screenY, layerMask)
        //   miss -> [0.0]; hit -> [1.0, entityId, px, py, pz, nx, ny, nz, distance]
        //   (same 9-float layout as _native_physics_raycast so scripts reuse RaycastHit)
        interpreter->registerNativeFunction("_native_picker_pickEntity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return missArray();

                events::input::PickEntityQuery q;
                q.screenPos = glm::vec2(extractFloat(args[0]), extractFloat(args[1]));
                q.layerMask = (args.size() >= 3) ? resolveLayerMask(extractString(args[2])) : 0xFFFF;

                services::RaycastHit hit = dispatcher.query(q);
                if (!hit.hit) return missArray();

                auto result = std::make_shared<value::NativeArray>(9, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(static_cast<float>(hit.entity.id)));
                result->set(2, value::Value(hit.point.x));
                result->set(3, value::Value(hit.point.y));
                result->set(4, value::Value(hit.point.z));
                result->set(5, value::Value(hit.normal.x));
                result->set(6, value::Value(hit.normal.y));
                result->set(7, value::Value(hit.normal.z));
                result->set(8, value::Value(hit.distance));
                return value::Value(result);
            }});

        // _native_picker_pickTerrainPoint(screenX, screenY)
        //   miss -> [0.0]; hit -> [1.0, px, py, pz]
        //   Heightfield pick: builds the screen ray, then ray-marches the CPU
        //   heightfield (GetTerrainHeightAtQuery, no physics) so it works at runtime
        //   even when the terrain has no physics collider. Complements pickEntity's
        //   physics raycast (Picker::pickTerrainPhysics).
        interpreter->registerNativeFunction("_native_picker_pickTerrainPoint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return missArray();

                events::input::ScreenToWorldRayQuery rayQuery;
                rayQuery.screenPos = glm::vec2(extractFloat(args[0]), extractFloat(args[1]));
                auto ray = dispatcher.query(rayQuery);
                if (!ray.has_value()) return missArray();

                const glm::vec3 origin = ray->origin;
                glm::vec3 dir = ray->direction;
                const float dirLen = glm::length(dir);
                if (dirLen < 1e-6f) return missArray();
                dir /= dirLen;

                auto sampleGap = [&](float t, bool& valid) -> float
                {
                    const glm::vec3 p = origin + dir * t;
                    events::terrain::GetTerrainHeightAtQuery hq;
                    hq.worldX = p.x;
                    hq.worldZ = p.z;
                    terrain::TerrainHeightAtResult r = dispatcher.query(hq);
                    valid = r.valid;
                    return p.y - r.height;   // >0 above surface, <=0 below
                };

                // Coarse march to bracket the surface crossing, then bisect.
                const float maxDist = 2000.0f;
                const float step = 2.0f;
                bool havePrev = false;
                float prevT = 0.0f;
                bool hit = false;
                float lo = 0.0f;
                float hi = 0.0f;

                for (float t = 0.0f; t <= maxDist; t += step)
                {
                    bool valid = false;
                    const float gap = sampleGap(t, valid);
                    if (!valid)
                    {
                        havePrev = false;   // off the loaded terrain; reset the bracket
                        continue;
                    }
                    if (havePrev && gap <= 0.0f)
                    {
                        lo = prevT;   // last point above the surface
                        hi = t;       // first point at/below the surface
                        hit = true;
                        break;
                    }
                    prevT = t;
                    havePrev = true;
                }

                if (!hit) return missArray();

                for (int i = 0; i < 24; ++i)
                {
                    const float mid = 0.5f * (lo + hi);
                    bool valid = false;
                    const float gap = sampleGap(mid, valid);
                    if (valid && gap > 0.0f) lo = mid; else hi = mid;
                }

                const glm::vec3 p = origin + dir * hi;
                auto result = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(p.x));
                result->set(2, value::Value(p.y));
                result->set(3, value::Value(p.z));
                return value::Value(result);
            }});

        // _native_picker_pickRegion(minX, minY, maxX, maxY, layerMask?)
        //   Returns an int[] of entity ids whose center falls inside the camera
        //   frustum of the screen sub-rectangle (RTS drag-select). Empty int[] when
        //   nothing is inside or there is no primary camera. Corner order is free
        //   (inverted drag normalized engine-side). layerMask is accepted for API
        //   symmetry but not applied here (see RuntimePickerAdapter::pickRegion) —
        //   scripts post-filter by their own selection component.
        interpreter->registerNativeFunction("_native_picker_pickRegion",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4)
                    return value::Value(std::make_shared<value::NativeArray>(0, value::ValueType::INT));

                events::input::PickRegionQuery q;
                q.minPx = glm::vec2(extractFloat(args[0]), extractFloat(args[1]));
                q.maxPx = glm::vec2(extractFloat(args[2]), extractFloat(args[3]));
                q.layerMask = (args.size() >= 5) ? resolveLayerMask(extractString(args[4])) : 0xFFFF;

                std::vector<services::EntityHandle> ids = dispatcher.query(q);

                auto arr = std::make_shared<value::NativeArray>(ids.size(), value::ValueType::INT);
                for (std::size_t i = 0; i < ids.size(); ++i)
                    arr->set(i, value::Value(static_cast<int64_t>(static_cast<uint32_t>(ids[i].id))));
                return value::Value(arr);
            }});

        vfLogInfo("[RuntimePickerAPI] Registered Picker native functions");
    }
}
