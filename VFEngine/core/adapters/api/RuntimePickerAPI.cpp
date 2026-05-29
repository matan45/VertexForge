// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "RuntimePickerAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/input/RuntimePickerEvents.hpp"
#include "../../../services/events/physics/PhysicsSettingsEvents.hpp"

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

        // _native_picker_pickTerrainPoint(screenX, screenY)
        //   miss -> [0.0]; hit -> [1.0, x, y, z]
        interpreter->registerNativeFunction("_native_picker_pickTerrainPoint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return missArray();

                events::input::PickTerrainQuery q;
                q.screenPos = glm::vec2(extractFloat(args[0]), extractFloat(args[1]));
                auto hit = dispatcher.query(q);
                if (!hit.has_value()) return missArray();

                auto result = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(hit->x));
                result->set(2, value::Value(hit->y));
                result->set(3, value::Value(hit->z));
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

        vfLogInfo("[RuntimePickerAPI] Registered Picker native functions");
    }
}
