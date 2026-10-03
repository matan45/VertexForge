#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        // ------------------------------------------------------------------
        // JSON <-> DTO field helpers
        // ------------------------------------------------------------------

        template <typename TEnum>
        struct EnumName
        {
            TEnum value;
            const char* name;
        };

        constexpr std::array<EnumName<::types::ColliderShape>, 6> colliderShapeNames{{
            {::types::ColliderShape::Box, "Box"},
            {::types::ColliderShape::Sphere, "Sphere"},
            {::types::ColliderShape::Capsule, "Capsule"},
            {::types::ColliderShape::ConvexMesh, "ConvexMesh"},
            {::types::ColliderShape::TriangleMesh, "TriangleMesh"},
            {::types::ColliderShape::HeightField, "HeightField"}
        }};

        constexpr std::array<EnumName<::types::RigidBodyType>, 3> rigidBodyTypeNames{{
            {::types::RigidBodyType::Static, "Static"},
            {::types::RigidBodyType::Dynamic, "Dynamic"},
            {::types::RigidBodyType::Kinematic, "Kinematic"}
        }};

        constexpr std::array<EnumName<::types::AudioPlayOrder>, 3> playOrderNames{{
            {::types::AudioPlayOrder::Single, "Single"},
            {::types::AudioPlayOrder::Random, "Random"},
            {::types::AudioPlayOrder::RoundRobin, "RoundRobin"}
        }};

        template <typename TEnum, std::size_t N>
        nlohmann::json enumToJson(TEnum value, const std::array<EnumName<TEnum>, N>& names)
        {
            for (const auto& entry : names)
            {
                if (entry.value == value)
                {
                    return entry.name;
                }
            }
            return static_cast<int64_t>(value);
        }

        template <typename TEnum, std::size_t N>
        void readEnum(const nlohmann::json& object, const char* key, TEnum& out,
                      const std::array<EnumName<TEnum>, N>& names)
        {
            auto it = object.find(key);
            if (it == object.end())
            {
                return;
            }
            if (it->is_string())
            {
                for (const auto& entry : names)
                {
                    if (*it == entry.name)
                    {
                        out = entry.value;
                        return;
                    }
                }
            }
            std::string allowed;
            for (const auto& entry : names)
            {
                allowed += allowed.empty() ? "" : ", ";
                allowed += entry.name;
            }
            throw ArgError(std::string("field '") + key + "' must be one of: " + allowed);
        }

        // Assets are exchanged as paths: absolute, or relative to the project working
        // directory. "guid:<hex>" is accepted too (and returned for assets whose
        // file can no longer be resolved). "" clears the reference.
        nlohmann::json assetToJson(const asset::AssetRef& ref)
        {
            if (!ref.isValid())
            {
                return "";
            }
            const std::string& path = ref.resolve();
            return path.empty() ? "guid:" + ref.toHexString() : path;
        }

        asset::AssetRef assetFromJson(const nlohmann::json& value, const char* key)
        {
            if (value.is_null())
            {
                return asset::AssetRef::invalid();
            }
            if (!value.is_string())
            {
                throw ArgError(std::string("field '") + key + "' must be an asset path string");
            }
            const std::string text = value.get<std::string>();
            if (text.empty())
            {
                return asset::AssetRef::invalid();
            }

            static constexpr std::string_view guidPrefix = "guid:";
            asset::AssetRef ref = text.starts_with(guidPrefix)
                ? asset::AssetRef::fromHexString(text.substr(guidPrefix.size()))
                : asset::AssetRef::fromPath(text);
            if (!ref.isValid())
            {
                throw ArgError(std::string("field '") + key + "': asset not found or not an engine asset: " + text +
                               " (use assets_list to find imported assets)");
            }
            return ref;
        }

        nlohmann::json assetListToJson(const std::vector<asset::AssetRef>& refs)
        {
            nlohmann::json out = nlohmann::json::array();
            for (const asset::AssetRef& ref : refs)
            {
                out.push_back(assetToJson(ref));
            }
            return out;
        }

        template <typename T>
        void readField(const nlohmann::json& object, const char* key, T& out)
        {
            auto it = object.find(key);
            if (it == object.end())
            {
                return;
            }
            const nlohmann::json& value = *it;

            if constexpr (std::is_same_v<T, bool>)
            {
                if (!value.is_boolean())
                {
                    throw ArgError(std::string("field '") + key + "' must be a boolean");
                }
                out = value.get<bool>();
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                if (!value.is_number())
                {
                    throw ArgError(std::string("field '") + key + "' must be a number");
                }
                out = value.get<float>();
            }
            else if constexpr (std::is_integral_v<T>)
            {
                int64_t number = 0;
                if (value.is_number_integer())
                {
                    number = value.get<int64_t>();
                }
                else if (value.is_number_float() &&
                         value.get<double>() == static_cast<double>(static_cast<int64_t>(value.get<double>())))
                {
                    number = static_cast<int64_t>(value.get<double>());
                }
                else
                {
                    throw ArgError(std::string("field '") + key + "' must be an integer");
                }
                if (number < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
                    number > static_cast<int64_t>(std::numeric_limits<T>::max()))
                {
                    throw ArgError(std::string("field '") + key + "' is out of range [" +
                                   std::to_string(static_cast<int64_t>(std::numeric_limits<T>::min())) + ", " +
                                   std::to_string(static_cast<int64_t>(std::numeric_limits<T>::max())) + "]");
                }
                out = static_cast<T>(number);
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                out = ArgReader::toVec3(value, key);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                if (!value.is_string())
                {
                    throw ArgError(std::string("field '") + key + "' must be a string");
                }
                out = value.get<std::string>();
            }
            else if constexpr (std::is_same_v<T, asset::AssetRef>)
            {
                out = assetFromJson(value, key);
            }
            else if constexpr (std::is_same_v<T, std::vector<asset::AssetRef>>)
            {
                if (!value.is_array())
                {
                    throw ArgError(std::string("field '") + key + "' must be an array of asset paths");
                }
                std::vector<asset::AssetRef> refs;
                refs.reserve(value.size());
                for (const nlohmann::json& item : value)
                {
                    refs.push_back(assetFromJson(item, key));
                }
                out = std::move(refs);
            }
            else
            {
                static_assert(!sizeof(T*), "unsupported component field type");
            }
        }

        // ------------------------------------------------------------------
        // Per-type mappers. Keys are the DTO field names verbatim.
        // ------------------------------------------------------------------

        nlohmann::json meshToJson(const services::MeshData& d)
        {
            return {
                {"meshRef", assetToJson(d.meshRef)},
                {"animatorRef", assetToJson(d.animatorRef)},
                {"retargetRef", assetToJson(d.retargetRef)},
                {"showBoundingBox", d.showBoundingBox},
                {"applyRootMotion", d.applyRootMotion},
                {"maxDrawDistance", d.maxDrawDistance},
                {"submeshIndex", d.submeshIndex},
                {"renderLayer", d.renderLayer}
            };
        }

        void meshFromJson(const nlohmann::json& j, services::MeshData& d)
        {
            readField(j, "meshRef", d.meshRef);
            readField(j, "animatorRef", d.animatorRef);
            readField(j, "retargetRef", d.retargetRef);
            readField(j, "showBoundingBox", d.showBoundingBox);
            readField(j, "applyRootMotion", d.applyRootMotion);
            readField(j, "maxDrawDistance", d.maxDrawDistance);
            readField(j, "submeshIndex", d.submeshIndex);
            readField(j, "renderLayer", d.renderLayer);
            if (d.renderLayer > 31)
            {
                throw ArgError("field 'renderLayer' must be 0-31");
            }
        }

        nlohmann::json cameraToJson(const services::CameraData& d)
        {
            return {
                {"fieldOfView", d.fieldOfView},
                {"nearPlane", d.nearPlane},
                {"farPlane", d.farPlane},
                {"aspectRatio", d.aspectRatio},
                {"isPerspective", d.isPerspective},
                {"isPrimary", d.isPrimary},
                {"showFrustum", d.showFrustum},
                {"orthoSize", d.orthoSize},
                {"cullingMask", d.cullingMask}
            };
        }

        void cameraFromJson(const nlohmann::json& j, services::CameraData& d)
        {
            readField(j, "fieldOfView", d.fieldOfView);
            readField(j, "nearPlane", d.nearPlane);
            readField(j, "farPlane", d.farPlane);
            readField(j, "aspectRatio", d.aspectRatio);
            readField(j, "isPerspective", d.isPerspective);
            readField(j, "isPrimary", d.isPrimary);
            readField(j, "showFrustum", d.showFrustum);
            readField(j, "orthoSize", d.orthoSize);
            readField(j, "cullingMask", d.cullingMask);
        }

        nlohmann::json directionalLightToJson(const services::DirectionalLightData& d)
        {
            return {
                {"color", vec3ToJson(d.color)},
                {"intensity", d.intensity},
                {"lightSize", d.lightSize},
                {"showGizmo", d.showGizmo}
            };
        }

        void directionalLightFromJson(const nlohmann::json& j, services::DirectionalLightData& d)
        {
            readField(j, "color", d.color);
            readField(j, "intensity", d.intensity);
            readField(j, "lightSize", d.lightSize);
            readField(j, "showGizmo", d.showGizmo);
        }

        nlohmann::json pointLightToJson(const services::PointLightData& d)
        {
            return {
                {"color", vec3ToJson(d.color)},
                {"intensity", d.intensity},
                {"radius", d.radius},
                {"lightSize", d.lightSize},
                {"castsShadow", d.castsShadow},
                {"showGizmo", d.showGizmo}
            };
        }

        void pointLightFromJson(const nlohmann::json& j, services::PointLightData& d)
        {
            readField(j, "color", d.color);
            readField(j, "intensity", d.intensity);
            readField(j, "radius", d.radius);
            readField(j, "lightSize", d.lightSize);
            readField(j, "castsShadow", d.castsShadow);
            readField(j, "showGizmo", d.showGizmo);
        }

        nlohmann::json spotLightToJson(const services::SpotLightData& d)
        {
            return {
                {"color", vec3ToJson(d.color)},
                {"intensity", d.intensity},
                {"innerAngle", d.innerAngle},
                {"outerAngle", d.outerAngle},
                {"range", d.range},
                {"lightSize", d.lightSize},
                {"castsShadow", d.castsShadow},
                {"showGizmo", d.showGizmo}
            };
        }

        void spotLightFromJson(const nlohmann::json& j, services::SpotLightData& d)
        {
            readField(j, "color", d.color);
            readField(j, "intensity", d.intensity);
            readField(j, "innerAngle", d.innerAngle);
            readField(j, "outerAngle", d.outerAngle);
            readField(j, "range", d.range);
            readField(j, "lightSize", d.lightSize);
            readField(j, "castsShadow", d.castsShadow);
            readField(j, "showGizmo", d.showGizmo);
        }

        nlohmann::json rigidBodyToJson(const services::RigidBodyComponentData& d)
        {
            return {
                {"type", enumToJson(d.type, rigidBodyTypeNames)},
                {"mass", d.mass},
                {"linearDamping", d.linearDamping},
                {"angularDamping", d.angularDamping},
                {"freezePositionX", d.freezePositionX},
                {"freezePositionY", d.freezePositionY},
                {"freezePositionZ", d.freezePositionZ},
                {"freezeRotationX", d.freezeRotationX},
                {"freezeRotationY", d.freezeRotationY},
                {"freezeRotationZ", d.freezeRotationZ}
            };
        }

        void rigidBodyFromJson(const nlohmann::json& j, services::RigidBodyComponentData& d)
        {
            readEnum(j, "type", d.type, rigidBodyTypeNames);
            readField(j, "mass", d.mass);
            readField(j, "linearDamping", d.linearDamping);
            readField(j, "angularDamping", d.angularDamping);
            readField(j, "freezePositionX", d.freezePositionX);
            readField(j, "freezePositionY", d.freezePositionY);
            readField(j, "freezePositionZ", d.freezePositionZ);
            readField(j, "freezeRotationX", d.freezeRotationX);
            readField(j, "freezeRotationY", d.freezeRotationY);
            readField(j, "freezeRotationZ", d.freezeRotationZ);
        }

        nlohmann::json colliderToJson(const services::ColliderComponentData& d)
        {
            return {
                {"shape", enumToJson(d.shape, colliderShapeNames)},
                {"size", vec3ToJson(d.size)},
                {"height", d.height},
                {"offset", vec3ToJson(d.offset)},
                {"meshRef", assetToJson(d.meshRef)},
                {"submeshIndex", d.submeshIndex},
                {"isTrigger", d.isTrigger},
                {"collisionLayer", d.collisionLayer},
                {"friction", d.friction},
                {"restitution", d.restitution}
            };
        }

        void colliderFromJson(const nlohmann::json& j, services::ColliderComponentData& d)
        {
            readEnum(j, "shape", d.shape, colliderShapeNames);
            readField(j, "size", d.size);
            readField(j, "height", d.height);
            readField(j, "offset", d.offset);
            readField(j, "meshRef", d.meshRef);
            readField(j, "submeshIndex", d.submeshIndex);
            readField(j, "isTrigger", d.isTrigger);
            readField(j, "collisionLayer", d.collisionLayer);
            readField(j, "friction", d.friction);
            readField(j, "restitution", d.restitution);
            if (d.collisionLayer > 15)
            {
                throw ArgError("field 'collisionLayer' must be 0-15");
            }
        }

        nlohmann::json audioSource2DToJson(const services::AudioSource2DData& d)
        {
            return {
                {"audioRef", assetToJson(d.audioRef)},
                {"volume", d.volume},
                {"pitch", d.pitch},
                {"loop", d.loop},
                {"busName", d.busName},
                {"fadeInMs", d.fadeInMs},
                {"clipVariants", assetListToJson(d.clipVariants)},
                {"playOrder", enumToJson(d.playOrder, playOrderNames)},
                {"pitchVariation", d.pitchVariation},
                {"volumeVariation", d.volumeVariation}
            };
        }

        void audioSource2DFromJson(const nlohmann::json& j, services::AudioSource2DData& d)
        {
            readField(j, "audioRef", d.audioRef);
            readField(j, "volume", d.volume);
            readField(j, "pitch", d.pitch);
            readField(j, "loop", d.loop);
            readField(j, "busName", d.busName);
            readField(j, "fadeInMs", d.fadeInMs);
            readField(j, "clipVariants", d.clipVariants);
            readEnum(j, "playOrder", d.playOrder, playOrderNames);
            readField(j, "pitchVariation", d.pitchVariation);
            readField(j, "volumeVariation", d.volumeVariation);
        }

        nlohmann::json audioSource3DToJson(const services::AudioSource3DData& d)
        {
            return {
                {"audioRef", assetToJson(d.audioRef)},
                {"volume", d.volume},
                {"pitch", d.pitch},
                {"loop", d.loop},
                {"minDistance", d.minDistance},
                {"maxDistance", d.maxDistance},
                {"showDebugSpheres", d.showDebugSpheres},
                {"enableDistanceFilter", d.enableDistanceFilter},
                {"filterStartDistance", d.filterStartDistance},
                {"filterMaxDistance", d.filterMaxDistance},
                {"filterIntensity", d.filterIntensity},
                {"enableOcclusion", d.enableOcclusion},
                {"occlusionLpf", d.occlusionLpf},
                {"occlusionVolume", d.occlusionVolume},
                {"occlusionLayerMask", d.occlusionLayerMask},
                {"innerConeAngle", d.innerConeAngle},
                {"outerConeAngle", d.outerConeAngle},
                {"outerConeGain", d.outerConeGain},
                {"showDebugCone", d.showDebugCone},
                {"busName", d.busName},
                {"priority", d.priority},
                {"fadeInMs", d.fadeInMs},
                {"clipVariants", assetListToJson(d.clipVariants)},
                {"playOrder", enumToJson(d.playOrder, playOrderNames)},
                {"pitchVariation", d.pitchVariation},
                {"volumeVariation", d.volumeVariation}
            };
        }

        void audioSource3DFromJson(const nlohmann::json& j, services::AudioSource3DData& d)
        {
            readField(j, "audioRef", d.audioRef);
            readField(j, "volume", d.volume);
            readField(j, "pitch", d.pitch);
            readField(j, "loop", d.loop);
            readField(j, "minDistance", d.minDistance);
            readField(j, "maxDistance", d.maxDistance);
            readField(j, "showDebugSpheres", d.showDebugSpheres);
            readField(j, "enableDistanceFilter", d.enableDistanceFilter);
            readField(j, "filterStartDistance", d.filterStartDistance);
            readField(j, "filterMaxDistance", d.filterMaxDistance);
            readField(j, "filterIntensity", d.filterIntensity);
            readField(j, "enableOcclusion", d.enableOcclusion);
            readField(j, "occlusionLpf", d.occlusionLpf);
            readField(j, "occlusionVolume", d.occlusionVolume);
            readField(j, "occlusionLayerMask", d.occlusionLayerMask);
            readField(j, "innerConeAngle", d.innerConeAngle);
            readField(j, "outerConeAngle", d.outerConeAngle);
            readField(j, "outerConeGain", d.outerConeGain);
            readField(j, "showDebugCone", d.showDebugCone);
            readField(j, "busName", d.busName);
            readField(j, "priority", d.priority);
            readField(j, "fadeInMs", d.fadeInMs);
            readField(j, "clipVariants", d.clipVariants);
            readEnum(j, "playOrder", d.playOrder, playOrderNames);
            readField(j, "pitchVariation", d.pitchVariation);
            readField(j, "volumeVariation", d.volumeVariation);
        }

        // ------------------------------------------------------------------
        // Binding table
        // ------------------------------------------------------------------

        struct ComponentBinding
        {
            std::string name;
            std::string fieldHelp;
            std::function<bool(const services::EntityHandle&)> add;
            std::function<bool(const services::EntityHandle&)> remove;
            // nullopt = the entity has no such component.
            std::function<std::optional<nlohmann::json>(const services::EntityHandle&)> get;
            // Overlays `patch` onto the current data and writes it back. Returns the
            // data read back afterwards, or nullopt when the component is missing.
            std::function<std::optional<nlohmann::json>(const services::EntityHandle&, const nlohmann::json&)> patch;
        };

        // Every curated component follows the same event shape:
        // Add<X>ComponentCommand / Remove<X>ComponentCommand {entity} -> bool,
        // Get<X>DataQuery {entity} -> optional<Data>, Set<X>DataCommand {entity, <member>} -> bool.
        template <typename TAdd, typename TRemove, typename TGet, typename TSet, typename TData>
        ComponentBinding makeBinding(std::string name, std::string fieldHelp, TData TSet::*dataMember,
                                     nlohmann::json (*toJson)(const TData&),
                                     void (*fromJson)(const nlohmann::json&, TData&))
        {
            static_assert(std::is_same_v<typename TGet::ResultType, std::optional<TData>>,
                          "Get query must return optional<Data>");
            static_assert(std::is_same_v<typename TAdd::ResultType, bool>, "Add command must return bool");
            static_assert(std::is_same_v<typename TRemove::ResultType, bool>, "Remove command must return bool");
            static_assert(std::is_same_v<typename TSet::ResultType, bool>, "Set command must return bool");

            ComponentBinding binding;
            binding.name = name;
            binding.fieldHelp = std::move(fieldHelp);
            binding.add = [](const services::EntityHandle& entity)
            {
                TAdd command;
                command.entity = entity;
                return events::EventDispatcher::instance().execute(command);
            };
            binding.remove = [](const services::EntityHandle& entity)
            {
                TRemove command;
                command.entity = entity;
                return events::EventDispatcher::instance().execute(command);
            };
            binding.get = [toJson](const services::EntityHandle& entity) -> std::optional<nlohmann::json>
            {
                TGet query;
                query.entity = entity;
                auto data = events::EventDispatcher::instance().query(query);
                if (!data.has_value())
                {
                    return std::nullopt;
                }
                return toJson(*data);
            };
            binding.patch = [name, dataMember, toJson, fromJson](const services::EntityHandle& entity,
                                                                 const nlohmann::json& patch) -> std::optional<nlohmann::json>
            {
                auto& dispatcher = events::EventDispatcher::instance();

                TGet query;
                query.entity = entity;
                auto current = dispatcher.query(query);
                if (!current.has_value())
                {
                    return std::nullopt;
                }

                TData data = *current;
                const nlohmann::json known = toJson(data);
                for (auto it = patch.begin(); it != patch.end(); ++it)
                {
                    if (!known.contains(it.key()))
                    {
                        std::string allowed;
                        for (auto field = known.begin(); field != known.end(); ++field)
                        {
                            allowed += allowed.empty() ? "" : ", ";
                            allowed += field.key();
                        }
                        throw ArgError("unknown " + name + " field '" + it.key() + "'; valid fields: " + allowed);
                    }
                }
                fromJson(patch, data);

                TSet command;
                command.entity = entity;
                command.*dataMember = data;
                if (!dispatcher.execute(command))
                {
                    throw std::runtime_error("The engine rejected the " + name +
                                             " data (a value is out of its valid range, e.g. negative intensity/radius)");
                }

                auto written = dispatcher.query(query);
                return toJson(written.has_value() ? *written : data);
            };
            return binding;
        }

        const std::vector<ComponentBinding>& bindings()
        {
            using namespace events::scene;
            static const std::vector<ComponentBinding> table{
                makeBinding<AddMeshComponentCommand, RemoveMeshComponentCommand, GetMeshDataQuery, SetMeshDataCommand>(
                    "Mesh",
                    "meshRef (asset path of an imported .vfMesh), animatorRef (.vfAnimator path or \"\"), "
                    "retargetRef (.vfretarget path or \"\"), showBoundingBox (bool), applyRootMotion (bool), "
                    "maxDrawDistance (float, 0 = category default), submeshIndex (int, -1 = all), renderLayer (0-31). "
                    "Materials are assigned with material_assign, not here.",
                    &SetMeshDataCommand::meshData, &meshToJson, &meshFromJson),
                makeBinding<AddCameraComponentCommand, RemoveCameraComponentCommand, GetCameraDataQuery, SetCameraDataCommand>(
                    "Camera",
                    "fieldOfView (degrees), nearPlane, farPlane, aspectRatio, isPerspective (bool), "
                    "isPrimary (bool - the primary game camera), showFrustum (bool), orthoSize, "
                    "cullingMask (uint32 render-layer mask)",
                    &SetCameraDataCommand::cameraData, &cameraToJson, &cameraFromJson),
                makeBinding<AddDirectionalLightComponentCommand, RemoveDirectionalLightComponentCommand,
                            GetDirectionalLightDataQuery, SetDirectionalLightDataCommand>(
                    "DirectionalLight",
                    "color [r,g,b] 0-1, intensity (>= 0), lightSize, showGizmo (bool). "
                    "Direction comes from the entity rotation.",
                    &SetDirectionalLightDataCommand::lightData, &directionalLightToJson, &directionalLightFromJson),
                makeBinding<AddPointLightComponentCommand, RemovePointLightComponentCommand,
                            GetPointLightDataQuery, SetPointLightDataCommand>(
                    "PointLight",
                    "color [r,g,b] 0-1, intensity (>= 0), radius (> 0), lightSize, castsShadow (bool), showGizmo (bool)",
                    &SetPointLightDataCommand::lightData, &pointLightToJson, &pointLightFromJson),
                makeBinding<AddSpotLightComponentCommand, RemoveSpotLightComponentCommand,
                            GetSpotLightDataQuery, SetSpotLightDataCommand>(
                    "SpotLight",
                    "color [r,g,b] 0-1, intensity (>= 0), innerAngle / outerAngle (degrees), range, lightSize, "
                    "castsShadow (bool), showGizmo (bool). Orientation comes from the entity rotation.",
                    &SetSpotLightDataCommand::lightData, &spotLightToJson, &spotLightFromJson),
                makeBinding<AddRigidBodyComponentCommand, RemoveRigidBodyComponentCommand,
                            GetRigidBodyDataQuery, SetRigidBodyDataCommand>(
                    "RigidBody",
                    "type (Static | Dynamic | Kinematic), mass, linearDamping, angularDamping, "
                    "freezePositionX/Y/Z (bool), freezeRotationX/Y/Z (bool). Pair it with a Collider.",
                    &SetRigidBodyDataCommand::rigidBodyData, &rigidBodyToJson, &rigidBodyFromJson),
                makeBinding<AddColliderComponentCommand, RemoveColliderComponentCommand,
                            GetColliderDataQuery, SetColliderDataCommand>(
                    "Collider",
                    "shape (Box | Sphere | Capsule | ConvexMesh | TriangleMesh | HeightField), size [x,y,z] "
                    "(shape dimensions - same meaning as the inspector's Size field), height (Capsule), "
                    "offset [x,y,z], meshRef (mesh asset path for ConvexMesh/TriangleMesh), submeshIndex (int, -1 = all), "
                    "isTrigger (bool), collisionLayer (0-15), friction, restitution",
                    &SetColliderDataCommand::colliderData, &colliderToJson, &colliderFromJson),
                makeBinding<AddAudioSource3DComponentCommand, RemoveAudioSource3DComponentCommand,
                            GetAudioSource3DDataQuery, SetAudioSource3DDataCommand>(
                    "AudioSource3D",
                    "audioRef (.vfAudio path), volume, pitch, loop (bool), minDistance, maxDistance, showDebugSpheres, "
                    "enableDistanceFilter, filterStartDistance, filterMaxDistance, filterIntensity, enableOcclusion, "
                    "occlusionLpf, occlusionVolume, occlusionLayerMask (uint16), innerConeAngle, outerConeAngle, "
                    "outerConeGain, showDebugCone, busName (string), priority (0-255, lower = more important), "
                    "fadeInMs, clipVariants ([paths]), playOrder (Single | Random | RoundRobin), pitchVariation, volumeVariation",
                    &SetAudioSource3DDataCommand::audioData, &audioSource3DToJson, &audioSource3DFromJson),
                makeBinding<AddAudioSource2DComponentCommand, RemoveAudioSource2DComponentCommand,
                            GetAudioSource2DDataQuery, SetAudioSource2DDataCommand>(
                    "AudioSource2D",
                    "audioRef (.vfAudio path), volume, pitch, loop (bool), busName (string), fadeInMs, "
                    "clipVariants ([paths]), playOrder (Single | Random | RoundRobin), pitchVariation, volumeVariation",
                    &SetAudioSource2DDataCommand::audioData, &audioSource2DToJson, &audioSource2DFromJson)
            };
            return table;
        }

        const ComponentBinding& requireBinding(const ArgReader& reader)
        {
            std::string type = reader.requireString("type");
            for (const ComponentBinding& binding : bindings())
            {
                if (binding.name == type)
                {
                    return binding;
                }
            }
            std::string allowed;
            for (const ComponentBinding& binding : bindings())
            {
                allowed += allowed.empty() ? "" : ", ";
                allowed += binding.name;
            }
            throw ArgError("unknown component type '" + type + "'; supported: " + allowed +
                           " (use entity_instantiate_json for anything else)");
        }

        std::vector<std::string> typeNames()
        {
            std::vector<std::string> names;
            for (const ComponentBinding& binding : bindings())
            {
                names.push_back(binding.name);
            }
            return names;
        }

        std::string fieldReference()
        {
            std::string text = "\n\nFields per type (vectors are [x,y,z]; asset fields take a path absolute or "
                               "relative to the project working directory, \"\" clears):";
            for (const ComponentBinding& binding : bindings())
            {
                text += "\n- " + binding.name + ": " + binding.fieldHelp;
            }
            return text;
        }

        services::EntityHandle requireExistingEntity(const ArgReader& reader)
        {
            uint32_t id = reader.requireEntity("entity");
            services::EntityHandle handle;
            handle.id = static_cast<uint64_t>(id);

            events::scene::GetEntityQuery query;
            query.entity = handle;
            if (!events::EventDispatcher::instance().query(query).has_value())
            {
                throw std::runtime_error("Entity " + std::to_string(id) +
                                         " does not exist; use entity_find or scene_get_hierarchy");
            }
            return handle;
        }

        const nlohmann::json& requireDataObject(const ArgReader& reader)
        {
            const nlohmann::json& data = reader.raw("data");
            if (!data.is_object())
            {
                throw ArgError("argument 'data' must be an object of field -> value");
            }
            return data;
        }

        nlohmann::json typeSchema()
        {
            return schema::enumString("Component type", typeNames());
        }

        void registerComponentAdd(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_add";
            tool.title = "Add component";
            tool.description =
                "Add a component to an entity, optionally initialising fields from 'data' (same keys as "
                "component_set). Fails if the entity already has that component - use component_set instead. "
                "Returns the component's resulting fields." + fieldReference();
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", typeSchema()},
                {"data", schema::anyObject("Optional initial field values (partial)")}
            }, {"entity", "type"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityHandle entity = requireExistingEntity(reader);
                const ComponentBinding& binding = requireBinding(reader);
                const nlohmann::json* data = reader.has("data") ? &requireDataObject(reader) : nullptr;

                if (binding.get(entity).has_value())
                {
                    return ToolResult::error("Entity already has a " + binding.name + " component; use component_set");
                }
                if (!binding.add(entity))
                {
                    return ToolResult::error("Add" + binding.name + "Component failed");
                }

                std::optional<nlohmann::json> fields;
                try
                {
                    fields = data != nullptr ? binding.patch(entity, *data) : binding.get(entity);
                }
                catch (...)
                {
                    // Invalid 'data': don't leave a half-applied, default-valued component behind.
                    binding.remove(entity);
                    throw;
                }
                return ToolResult::ok({
                    {"entity", static_cast<uint32_t>(entity.id)},
                    {"type", binding.name},
                    {"data", fields.has_value() ? *fields : nlohmann::json(nullptr)}
                });
            };
            registry.add(std::move(tool));
        }

        void registerComponentRemove(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_remove";
            tool.title = "Remove component";
            tool.description = "Remove a component from an entity. Supported types: Mesh, Camera, DirectionalLight, "
                               "PointLight, SpotLight, RigidBody, Collider, AudioSource3D, AudioSource2D.";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", typeSchema()}
            }, {"entity", "type"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityHandle entity = requireExistingEntity(reader);
                const ComponentBinding& binding = requireBinding(reader);

                if (!binding.get(entity).has_value())
                {
                    return ToolResult::error("Entity has no " + binding.name + " component");
                }
                if (!binding.remove(entity))
                {
                    return ToolResult::error("Remove" + binding.name + "Component failed");
                }
                return ToolResult::ok({{"entity", static_cast<uint32_t>(entity.id)}, {"removed", binding.name}});
            };
            registry.add(std::move(tool));
        }

        void registerComponentGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_get";
            tool.title = "Get component";
            tool.description =
                "Read all fields of one component on an entity (errors if the entity lacks it). "
                "For component types not listed here use entity_get." + fieldReference();
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", typeSchema()}
            }, {"entity", "type"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityHandle entity = requireExistingEntity(reader);
                const ComponentBinding& binding = requireBinding(reader);

                std::optional<nlohmann::json> fields = binding.get(entity);
                if (!fields.has_value())
                {
                    return ToolResult::error("Entity has no " + binding.name + " component");
                }
                return ToolResult::ok({
                    {"entity", static_cast<uint32_t>(entity.id)},
                    {"type", binding.name},
                    {"data", std::move(*fields)}
                });
            };
            registry.add(std::move(tool));
        }

        void registerComponentSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "component_set";
            tool.title = "Set component fields";
            tool.description =
                "Partially update an existing component: only the keys present in 'data' change, everything "
                "else keeps its current value. Unknown keys are rejected. Returns the resulting fields."
                + fieldReference();
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"type", typeSchema()},
                {"data", schema::anyObject("Field values to change (partial)")}
            }, {"entity", "type", "data"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                services::EntityHandle entity = requireExistingEntity(reader);
                const ComponentBinding& binding = requireBinding(reader);
                const nlohmann::json& data = requireDataObject(reader);

                std::optional<nlohmann::json> fields = binding.patch(entity, data);
                if (!fields.has_value())
                {
                    return ToolResult::error("Entity has no " + binding.name + " component; use component_add");
                }
                return ToolResult::ok({
                    {"entity", static_cast<uint32_t>(entity.id)},
                    {"type", binding.name},
                    {"data", std::move(*fields)}
                });
            };
            registry.add(std::move(tool));
        }
    }

    void registerComponentTools(ToolRegistry& registry, const ToolContext&)
    {
        registerComponentAdd(registry);
        registerComponentRemove(registry);
        registerComponentGet(registry);
        registerComponentSet(registry);
    }
}
