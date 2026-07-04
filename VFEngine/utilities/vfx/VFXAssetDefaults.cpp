#include "VFXAsset.hpp"
#include "VFXEventTypes.hpp"
#include "../uuid/UUID.hpp"

namespace vfx
{
    namespace
    {
        void addCoreProperties(VFXNode& node)
        {
            node.properties["spawnRate"] = VFXProperty{
                "spawnRate", VFXPropertyType::Float,
                EmitterDefaults::SPAWN_RATE, 0.0f, 1000.0f
            };
            node.properties["lifetime"] = VFXProperty{
                "lifetime", VFXPropertyType::Float,
                EmitterDefaults::LIFETIME, 0.0f, 60.0f
            };
            node.properties["startSize"] = VFXProperty{
                "startSize", VFXPropertyType::Float,
                EmitterDefaults::START_SIZE, 0.0f, 100.0f
            };
            node.properties["startVelocity"] = VFXProperty{
                "startVelocity", VFXPropertyType::Vec3,
                glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f
            };
            node.properties["startColor"] = VFXProperty{
                "startColor", VFXPropertyType::Color,
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 1.0f
            };
            node.properties["looping"] = VFXProperty{
                "looping", VFXPropertyType::Bool,
                EmitterDefaults::LOOPING, 0.0f, 1.0f
            };
            node.properties["inheritVelocityRatio"] = VFXProperty{
                "inheritVelocityRatio", VFXPropertyType::Float,
                EmitterDefaults::INHERIT_VELOCITY_RATIO, 0.0f, 1.0f
            };
            node.properties["texture"] = VFXProperty{
                "texture", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };
        }

        void addFlipbookProperties(VFXNode& node)
        {
            node.properties["flipbookRows"] = VFXProperty{
                "flipbookRows", VFXPropertyType::Int,
                EmitterDefaults::FLIPBOOK_ROWS, 1.0f, 16.0f
            };
            node.properties["flipbookColumns"] = VFXProperty{
                "flipbookColumns", VFXPropertyType::Int,
                EmitterDefaults::FLIPBOOK_COLUMNS, 1.0f, 16.0f
            };
            node.properties["flipbookFrameRate"] = VFXProperty{
                "flipbookFrameRate", VFXPropertyType::Float,
                EmitterDefaults::FLIPBOOK_FRAME_RATE, 0.0f, 120.0f
            };
            node.properties["flipbookRandomStart"] = VFXProperty{
                "flipbookRandomStart", VFXPropertyType::Bool,
                EmitterDefaults::FLIPBOOK_RANDOM_START, 0.0f, 1.0f
            };
            node.properties["flipbookFrameBlend"] = VFXProperty{
                "flipbookFrameBlend", VFXPropertyType::Bool,
                EmitterDefaults::FLIPBOOK_FRAME_BLEND, 0.0f, 1.0f
            };
        }

        void addVarianceProperties(VFXNode& node)
        {
            node.properties["sizeVariance"] = VFXProperty{
                "sizeVariance", VFXPropertyType::Float,
                EmitterDefaults::SIZE_VARIANCE, 0.0f, 1.0f
            };
            node.properties["lifetimeVariance"] = VFXProperty{
                "lifetimeVariance", VFXPropertyType::Float,
                EmitterDefaults::LIFETIME_VARIANCE, 0.0f, 1.0f
            };
            node.properties["speedVariance"] = VFXProperty{
                "speedVariance", VFXPropertyType::Float,
                EmitterDefaults::SPEED_VARIANCE, 0.0f, 1.0f
            };
            node.properties["rotationVariance"] = VFXProperty{
                "rotationVariance", VFXPropertyType::Float,
                EmitterDefaults::ROTATION_VARIANCE_DEGREES, 0.0f, 180.0f
            };
            node.properties["angularVelocityVariance"] = VFXProperty{
                "angularVelocityVariance", VFXPropertyType::Float,
                EmitterDefaults::ANGULAR_VELOCITY_VARIANCE_DEGREES, 0.0f, 720.0f
            };
            node.properties["colorValueVariance"] = VFXProperty{
                "colorValueVariance", VFXPropertyType::Float,
                EmitterDefaults::COLOR_VALUE_VARIANCE, 0.0f, 1.0f
            };
            node.properties["alphaVariance"] = VFXProperty{
                "alphaVariance", VFXPropertyType::Float,
                EmitterDefaults::ALPHA_VARIANCE, 0.0f, 1.0f
            };
        }

        void addRenderingProperties(VFXNode& node)
        {
            node.properties["alphaClipThreshold"] = VFXProperty{
                "alphaClipThreshold", VFXPropertyType::Float,
                EmitterDefaults::ALPHA_CLIP_THRESHOLD, 0.0f, 1.0f
            };
            node.properties["additiveBlend"] = VFXProperty{
                "additiveBlend", VFXPropertyType::Bool,
                EmitterDefaults::ADDITIVE_BLEND, 0.0f, 1.0f
            };
            // VK-1472: blend mode supersedes the legacy additiveBlend bool. When
            // present it wins in the loader; the bool is kept above for back-compat.
            node.properties["blendMode"] = VFXProperty{
                "blendMode", VFXPropertyType::String,
                std::string(blendModeToString(EmitterDefaults::BLEND_MODE)), 0.0f, 0.0f
            };
            node.properties["sortOrder"] = VFXProperty{
                "sortOrder", VFXPropertyType::Int,
                EmitterDefaults::SORT_ORDER, -256.0f, 256.0f
            };
            node.properties["renderMode"] = VFXProperty{
                "renderMode", VFXPropertyType::Int,
                EmitterDefaults::RENDER_MODE, 0.0f, 4.0f
            };
            node.properties["softParticleDistance"] = VFXProperty{
                "softParticleDistance", VFXPropertyType::Float,
                EmitterDefaults::SOFT_PARTICLE_DISTANCE, 0.0f, 50.0f
            };
            node.properties["stretchMultiplier"] = VFXProperty{
                "stretchMultiplier", VFXPropertyType::Float,
                EmitterDefaults::STRETCH_MULTIPLIER, 0.1f, 10.0f
            };
            node.properties["meshPath"] = VFXProperty{
                "meshPath", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };
        }

        void addRibbonProperties(VFXNode& node)
        {
            node.properties["maxTrailPoints"] = VFXProperty{
                "maxTrailPoints", VFXPropertyType::Int,
                EmitterDefaults::MAX_TRAIL_POINTS, 2.0f, 256.0f
            };
            node.properties["ribbonWidth"] = VFXProperty{
                "ribbonWidth", VFXPropertyType::Float,
                EmitterDefaults::RIBBON_WIDTH, 0.01f, 10.0f
            };
            node.properties["ribbonMinDistance"] = VFXProperty{
                "ribbonMinDistance", VFXPropertyType::Float,
                EmitterDefaults::RIBBON_MIN_DISTANCE, 0.0f, 5.0f
            };
            // VK-1474: over-trail width curve + tail gradient. Defaults are no-ops (constant 1.0
            // width multiplier, opaque-white tint) so a new ribbon renders identically to the flat
            // legacy path until authored. min/max drive the curve editor's Y-range.
            node.properties["ribbonWidthCurve"] = VFXProperty{
                "ribbonWidthCurve", VFXPropertyType::Curve,
                VFXCurve::constant(1.0f), 0.0f, 4.0f
            };
            node.properties["ribbonTailGradient"] = VFXProperty{
                "ribbonTailGradient", VFXPropertyType::Gradient,
                VFXGradient::fromStartEnd(glm::vec4(1.0f), glm::vec4(1.0f)), 0.0f, 1.0f
            };
            node.properties["uvScrollSpeedU"] = VFXProperty{
                "uvScrollSpeedU", VFXPropertyType::Float,
                EmitterDefaults::UV_SCROLL_SPEED_U, -10.0f, 10.0f
            };
            node.properties["uvScrollSpeedV"] = VFXProperty{
                "uvScrollSpeedV", VFXPropertyType::Float,
                EmitterDefaults::UV_SCROLL_SPEED_V, -10.0f, 10.0f
            };
        }

        void addEventProperties(VFXNode& node)
        {
            VFXEventConfig config;
            config.types[eventTypeIndex(VFXEventType::OnSpawn)].enabled =
                EmitterDefaults::EVENT_ON_SPAWN_ENABLED;
            config.types[eventTypeIndex(VFXEventType::OnDeath)].enabled =
                EmitterDefaults::EVENT_ON_DEATH_ENABLED;
            config.types[eventTypeIndex(VFXEventType::OnCollision)].enabled =
                EmitterDefaults::EVENT_ON_COLLISION_ENABLED;
            config.types[eventTypeIndex(VFXEventType::OnLifetimeThreshold)].enabled =
                EmitterDefaults::EVENT_ON_LIFETIME_THRESHOLD_ENABLED;
            config.lifetimeThreshold = EmitterDefaults::EVENT_LIFETIME_THRESHOLD;
            storeEventConfigToNode(node, config);
        }

        void addLightingAndCollisionProperties(VFXNode& node)
        {
            node.properties["emissiveIntensity"] = VFXProperty{
                "emissiveIntensity", VFXPropertyType::Float,
                EmitterDefaults::EMISSIVE_INTENSITY, 0.0f, 100.0f
            };
            node.properties["lightingInfluence"] = VFXProperty{
                "lightingInfluence", VFXPropertyType::Float,
                EmitterDefaults::LIGHTING_INFLUENCE, 0.0f, 1.0f
            };
            node.properties["normalMode"] = VFXProperty{
                "normalMode", VFXPropertyType::Int,
                EmitterDefaults::NORMAL_MODE, 0.0f, 2.0f
            };
            node.properties["ambientAmount"] = VFXProperty{
                "ambientAmount", VFXPropertyType::Float,
                EmitterDefaults::AMBIENT_AMOUNT, 0.0f, 1.0f
            };
            node.properties["lightEmissionEnabled"] = VFXProperty{
                "lightEmissionEnabled", VFXPropertyType::Bool,
                EmitterDefaults::LIGHT_EMISSION_ENABLED, 0.0f, 1.0f
            };
            node.properties["lightEmissionIntensity"] = VFXProperty{
                "lightEmissionIntensity", VFXPropertyType::Float,
                EmitterDefaults::LIGHT_EMISSION_INTENSITY, 0.0f, 100.0f
            };
            node.properties["lightEmissionRadius"] = VFXProperty{
                "lightEmissionRadius", VFXPropertyType::Float,
                EmitterDefaults::LIGHT_EMISSION_RADIUS, 0.1f, 100.0f
            };
            node.properties["collisionEnabled"] = VFXProperty{
                "collisionEnabled", VFXPropertyType::Bool,
                EmitterDefaults::COLLISION_ENABLED, 0.0f, 1.0f
            };
            node.properties["collisionBounce"] = VFXProperty{
                "collisionBounce", VFXPropertyType::Float,
                EmitterDefaults::COLLISION_BOUNCE, 0.0f, 1.0f
            };
            node.properties["collisionFriction"] = VFXProperty{
                "collisionFriction", VFXPropertyType::Float,
                EmitterDefaults::COLLISION_FRICTION, 0.0f, 1.0f
            };
            node.properties["collisionLifetimeLoss"] = VFXProperty{
                "collisionLifetimeLoss", VFXPropertyType::Float,
                EmitterDefaults::COLLISION_LIFETIME_LOSS, 0.0f, 1.0f
            };
            node.properties["distortionEnabled"] = VFXProperty{
                "distortionEnabled", VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            node.properties["distortionStrength"] = VFXProperty{
                "distortionStrength", VFXPropertyType::Float,
                0.1f, 0.0f, 2.0f
            };
            node.properties["distortionTexture"] = VFXProperty{
                "distortionTexture", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };
        }

        VFXNode createDefaultEmitterNode(uint32_t nodeId)
        {
            VFXNode emitterNode;
            emitterNode.id = nodeId;
            emitterNode.type = VFXNodeType::Emitter;
            emitterNode.name = "Emitter";
            emitterNode.position = glm::vec2(100.0f, 200.0f);

            addCoreProperties(emitterNode);
            addVarianceProperties(emitterNode);
            addFlipbookProperties(emitterNode);
            addRenderingProperties(emitterNode);
            addRibbonProperties(emitterNode);
            addEventProperties(emitterNode);
            addLightingAndCollisionProperties(emitterNode);

            return emitterNode;
        }
    } // anonymous namespace

    VFXData VFXAsset::createDefault(const std::string& name)
    {
        VFXData vfxData;
        vfxData.uuid = std::to_string(uuid::UUID().getValue());
        vfxData.name = name;
        vfxData.version = VFX_FORMAT_VERSION;

        vfxData.graph.nodes.push_back(createDefaultEmitterNode(vfxData.graph.nextNodeId++));

        VFXNode outNode;
        outNode.id = vfxData.graph.nextNodeId++;
        outNode.type = VFXNodeType::OutSystem;
        outNode.name = "Output";
        outNode.position = glm::vec2(400.0f, 200.0f);
        vfxData.graph.nodes.push_back(std::move(outNode));

        VFXNodeLink link;
        link.id = vfxData.graph.nextLinkId++;
        link.sourceNodeId = 1;  // Emitter
        link.targetNodeId = 2;  // OutSystem
        link.sourcePin = "Output";
        link.targetPin = "Input";
        vfxData.graph.links.push_back(std::move(link));

        return vfxData;
    }
}
