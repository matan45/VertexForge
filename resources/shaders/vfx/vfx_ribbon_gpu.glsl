#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;
layout(location = 3) out float fragViewDepth;
layout(location = 4) out float fragGlowIntensity;
layout(location = 5) out vec3 fragWorldPos;
layout(location = 6) out vec3 fragNormal;
layout(location = 8) flat out uint fragEmitterSlot; // VK-1481 Phase 2: emitter slot for the FS (merged draw)

#include "vfx_gpu_types.glsl"
#include "vfx_lut.glsl"

const uint MAX_TRAIL_POINTS_STRIDE = 256u;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float nearPlane;
    float farPlane;
    float _pad1;
    float _pad2;
} camera;

layout(std430, set = 0, binding = 2) readonly buffer ParticleBuffer {
    GPUParticle particles[];
};

layout(std430, set = 0, binding = 3) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(std430, set = 0, binding = 5) readonly buffer RibbonRingBuffer {
    uint ribbonRing[];
};

layout(std430, set = 0, binding = 6) readonly buffer RibbonHeadBuffer {
    uint ribbonHeads[];
};

// VK-1474: baked LUT (shared with the compute sim buffer). Bound at binding 7 for the ribbon
// pipeline (binding 4 is the depth sampler here, unlike the compute pipeline where LUT is 4).
layout(std430, set = 0, binding = 7) readonly buffer LUTBuffer {
    vec4 lutData[];
};

layout(push_constant) uniform PushConstants {
    uint runBaseSlot; // VK-1481 Phase 2: emitterSlot = runBaseSlot + gl_DrawID
} pc;

// Linear-interpolated LUT fetch (duplicate of the compute sim's helper; the buffer binding
// differs between pipelines so the function can't live in the shared include).
vec4 sampleLUT(uint baseOffset, uint channelIndex, uint stride, float t)
{
    float coord = clamp(t, 0.0, 1.0) * float(stride - 1u);
    uint lower = uint(floor(coord));
    uint upper = min(lower + 1u, stride - 1u);
    float frac = coord - float(lower);
    uint channelOffset = baseOffset + channelIndex * stride;
    vec4 a = lutData[channelOffset + lower];
    vec4 b = lutData[channelOffset + upper];
    return mix(a, b, frac);
}

void main() {
    // VK-1481 Phase 2: recover this sub-draw's emitter slot; set the flat varying before any early
    // return so the FS always reads a valid slot even for culled (degenerate) ribbon segments.
    uint emitterSlot = pc.runBaseSlot + uint(gl_DrawID);
    fragEmitterSlot = emitterSlot;

    GPUEmitterConfig config = configs[emitterSlot];
    uint maxTP = config.maxTrailPoints;
    uint head = ribbonHeads[emitterSlot];
    uint segIdx = gl_InstanceIndex;

    uint ringBase = emitterSlot * MAX_TRAIL_POINTS_STRIDE;

    // Newest point = head-1, next newest = head-2, etc.
    // segIdx 0 connects the two newest points, segIdx 1 the next pair, etc.
    uint slotA = (head - 1u - segIdx) % maxTP;
    uint slotB = (head - 2u - segIdx) % maxTP;
    uint pidxA = ribbonRing[ringBase + slotA];
    uint pidxB = ribbonRing[ringBase + slotB];

    GPUParticle pA = particles[pidxA];
    GPUParticle pB = particles[pidxB];

    // Cull dead segments
    if (pA.size <= 0.0 || pB.size <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        fragViewDepth = 0.0;
        fragGlowIntensity = 0.0;
        fragWorldPos = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        return;
    }

    // inTexCoord.y selects which endpoint: 0 = pA (newer), 1 = pB (older)
    float along = inTexCoord.y;
    vec3 posA = pA.position;
    vec3 posB = pB.position;
    vec3 pos = mix(posA, posB, along);

    // Camera-facing ribbon: cross segment direction with view direction
    vec3 segDir = posB - posA;
    float segLen = length(segDir);
    if (segLen < 0.0001) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        fragViewDepth = 0.0;
        fragGlowIntensity = 0.0;
        fragWorldPos = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        return;
    }
    segDir /= segLen;

    // Per-vertex normalized trail position (0 = head, 1 = tail), normalized by LIVE segment
    // count (not capacity), matching the draw's instanceCount. Computed here so the width curve
    // and tail gradient can sample by it.
    uint totalSegments = min(head, maxTP) - 1u;
    float trailT = (totalSegments > 0u)
        ? (float(segIdx) + along) / float(totalSegments)
        : 0.0;

    vec3 toCamera = normalize(camera.cameraPos - pos);
    vec3 right = normalize(cross(toCamera, segDir));

    // VK-1474: flat ribbonWidth optionally scaled by an over-trail width curve (LUT ch7).
    float width = mix(pA.size, pB.size, along) * config.ribbonWidth;
    if ((config.lutFlags & LUT_FLAG_RIBBON_WIDTH) != 0u) {
        width *= sampleLUT(config.lutBaseOffset, LUT_CH_RIBBON_WIDTH, config.lutChannelStride, trailT).x;
    }
    pos += right * inPosition.x * width;

    // Tube-style lighting normal: blend the camera-facing normal with the
    // across-width direction so the ribbon shades like a cylinder
    vec3 facing = normalize(cross(right, segDir));
    if (dot(facing, toCamera) < 0.0) facing = -facing;
    float across = clamp(inPosition.x * 2.0, -1.0, 1.0);
    fragNormal = normalize(facing * sqrt(max(1.0 - across * across, 0.0)) + right * across);
    fragWorldPos = pos;

    gl_Position = camera.projection * camera.view * vec4(pos, 1.0);

    // View-space depth for soft particles
    fragViewDepth = -(camera.view * vec4(pos, 1.0)).z;

    // UV: U = trail position (0=head, 1=tail), V = across width (0..1)
    fragTexCoord = vec2(trailT, inTexCoord.x + 0.5);

    fragTexCoord += vec2(config.uvScrollSpeedU, config.uvScrollSpeedV) * camera.time;

    // Interpolate color and lifetime; VK-1474 optionally tint by an over-trail gradient (LUT ch8).
    fragColor = mix(pA.color, pB.color, along);
    if ((config.lutFlags & LUT_FLAG_RIBBON_TAIL_GRADIENT) != 0u) {
        fragColor *= sampleLUT(config.lutBaseOffset, LUT_CH_RIBBON_TAIL_GRADIENT, config.lutChannelStride, trailT);
    }
    float lifeA = (pA.maxLifetime > 0.0) ? (pA.lifetime / pA.maxLifetime) : 0.0;
    float lifeB = (pB.maxLifetime > 0.0) ? (pB.lifetime / pB.maxLifetime) : 0.0;
    fragLifetimeRatio = mix(lifeA, lifeB, along);
    fragGlowIntensity = mix(pA.glowIntensity, pB.glowIntensity, along);
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in float fragViewDepth;
layout(location = 4) in float fragGlowIntensity;
layout(location = 5) in vec3 fragWorldPos;
layout(location = 6) in vec3 fragNormal;
layout(location = 8) flat in uint fragEmitterSlot; // VK-1481 Phase 2: emitter slot (merged draw)

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float nearPlane;
    float farPlane;
    float _pad1;
    float _pad2;
} camera;

// VK-1481: shared VFX bindless texture table (bound once per pass). Per-emitter slot in renderData[].textureIndex.
layout(set = 4, binding = 0) uniform sampler2D bindlessTextures[];

#include "vfx_gpu_types.glsl"

layout(std430, set = 0, binding = 3) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

// VK-1481 Phase 2: per-emitter render data (textureIndex / alphaClip / blendMode / glow) for the merged draw.
layout(std430, set = 0, binding = 8) readonly buffer EmitterRenderDataBuffer {
    VFXEmitterRenderData renderData[];
};

layout(binding = 4) uniform sampler2D sceneDepthTexture;

// Lighting types (struct definitions + cluster helpers, no buffer references)
#include "../common/lighting_functions.glsl"
#include "../common/cluster_culling.glsl"

// Lighting descriptor sets (shared from main renderer)
layout(std430, set = 1, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

layout(std430, set = 1, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

layout(std430, set = 1, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

layout(std140, set = 1, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

layout(std140, set = 2, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

layout(std430, set = 3, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 3, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// VFX lighting evaluation (must come after buffer declarations above)
#include "vfx_lighting.glsl"

void main() {
    // VK-1481 Phase 2: per-emitter render data comes from the SSBO (indexed by the flat emitter
    // slot) instead of a push constant, so the draw can be merged into one multi-draw.
    VFXEmitterRenderData rd = renderData[fragEmitterSlot];

    vec4 texColor = texture(bindlessTextures[nonuniformEXT(rd.textureIndex)], fragTexCoord);
    vec4 finalColor = texColor * fragColor;

    // Soft particles: fade near scene geometry
    GPUEmitterConfig config = configs[fragEmitterSlot];
    if (config.softParticleDistance > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthTexture, 0));
        float rawDepth = texture(sceneDepthTexture, screenUV).r;

        float sceneLinearDepth = camera.nearPlane * camera.farPlane /
            (camera.farPlane - rawDepth * (camera.farPlane - camera.nearPlane));

        float depthDiff = sceneLinearDepth - fragViewDepth;
        float softFactor = clamp(depthDiff / config.softParticleDistance, 0.0, 1.0);
        finalColor.a *= softFactor;
    }

    // Scene lighting evaluation (tube normal from the vertex stage)
    if (config.lightingInfluence > 0.0 && rd.blendMode != 1u) {
        finalColor.rgb = evaluateVFXLighting(
            finalColor.rgb, normalize(fragNormal), fragWorldPos, fragViewDepth,
            config.ambientAmount, config.lightingInfluence);
    }

    // Glow: additive emissive color (applied after lighting)
    vec3 glowColor = vec3(rd.glowColorR, rd.glowColorG, rd.glowColorB);
    finalColor.rgb += glowColor * fragGlowIntensity;
    finalColor.rgb *= config.emissiveIntensity;

    if (finalColor.a < rd.alphaClipThreshold) {
        discard;
    }

    if (rd.blendMode == 1u) {
        // Additive: premultiplied rgb, zero alpha -> src.rgb + dst
        outColor = vec4(finalColor.rgb * finalColor.a, 0.0);
    } else if (rd.blendMode == 2u) {
        // Premultiplied: straight color + real alpha (fire->smoke gradient)
        outColor = finalColor;
    } else if (rd.blendMode == 3u) {
        // Multiply (dst*src): transparent = white so soft/alpha fade to no-op
        outColor = vec4(mix(vec3(1.0), finalColor.rgb, finalColor.a), finalColor.a);
    } else {
        // Alpha: premultiplied-over (identical result to the legacy straight-alpha path)
        outColor = vec4(finalColor.rgb * finalColor.a, finalColor.a);
    }
}
