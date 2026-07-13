#type COMPUTE
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "vfx_gpu_types.glsl"
#include "vfx_shape_placement.glsl" // VK-1525: SHAPE_* consts + ordered/ring placement math
#include "vfx_variance.glsl"
#include "vfx_lut.glsl"

const uint MODIFIER_COLOR_OVER_LIFETIME = 1u;
const uint MODIFIER_SIZE_OVER_LIFETIME = 2u;
const uint MODIFIER_SPEED_OVER_LIFETIME = 4u;
const uint MODIFIER_ROTATION_OVER_LIFETIME = 8u;
const uint MODIFIER_SIZE_BY_SPEED = 4194304u;  // 1 << 22 (VK-1473)
const uint MODIFIER_COLOR_BY_SPEED = 8388608u; // 1 << 23 (VK-1473)
const uint MODIFIER_DEPTH_COLLISION = 33554432u; // 1 << 25 (VK-1502)

const uint FORCE_GRAVITY = 16u;
const uint FORCE_WIND = 32u;
const uint FORCE_TURBULENCE = 64u;
const uint FORCE_VORTEX = 128u;
const uint FORCE_DRAG = 65536u;          // 1 << 16
const uint FORCE_ATTRACTOR = 131072u;    // 1 << 17
const uint FORCE_ATTRACTOR_KILL = 262144u; // 1 << 18
const uint FORCE_CURL_NOISE = 524288u;   // 1 << 19
const uint FORCE_KILL_VOLUME = 1048576u; // 1 << 20

// SHAPE_* constants (SHAPE_SPHERE/CONE/BOX/TORUS/RING/ORDERED/EMIT_FROM_SURFACE/RANDOM_DIRECTION)
// live in vfx_shape_placement.glsl (included above).

const uint MODIFIER_GLOW_OVER_LIFETIME = 32768u;

struct GPUEmitterState
{
    mat4 worldTransform;
    mat4 prevWorldTransform;
    vec4 emitterVelocityAndInherit; // xyz = emitter world velocity, w = inherit ratio
    uint particleOffset;
    uint maxParticles;
    uint activeCount;
    uint spawnThisFrame;
    float spawnAccumulator;
    uint flags;
    uint spawnCounter;
    uint padding;
};

struct VFXDrawIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

layout(std430, set = 0, binding = 0) buffer ParticleBuffer {
    GPUParticle particles[];
};

layout(std430, set = 0, binding = 1) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(std430, set = 0, binding = 2) buffer EmitterStateBuffer {
    GPUEmitterState states[];
};

layout(std430, set = 0, binding = 3) writeonly buffer DrawCommandBuffer {
    VFXDrawIndirectCommand drawCommands[];
};

layout(std430, set = 0, binding = 4) readonly buffer LUTBuffer {
    vec4 lutData[];
};

layout(std430, set = 0, binding = 5) buffer RibbonRingBuffer {
    uint ribbonRing[];
};

layout(std430, set = 0, binding = 6) buffer RibbonHeadBuffer {
    uint ribbonHeads[];
};

struct GPUVFXEvent
{
    vec3 position;
    uint eventType;
    vec3 velocity;
    uint emitterIndex;
    vec3 color;
    float size;
};

layout(std430, set = 0, binding = 7) buffer EventBuffer {
    uint eventCount;
    GPUVFXEvent events[];
};

struct GPUCollider
{
    vec4 positionAndType;   // xyz=center, w=type (0=Sphere, 1=Box, 2=Capsule)
    vec4 rotation;          // quaternion xyzw
    vec4 dimensions;        // Sphere: x=radius; Box: xyz=halfExtents; Capsule: x=radius, y=halfHeight
};

layout(std430, set = 0, binding = 8) readonly buffer ColliderBuffer {
    GPUCollider colliders[];
};

layout(std430, set = 0, binding = 9) readonly buffer TerrainHeightfieldBuffer {
    float terrainWorldOriginX;
    float terrainWorldOriginZ;
    float terrainTileWorldSize;
    float terrainVertexSpacing;
    int terrainGridCountX;
    int terrainGridCountZ;
    uint terrainVerticesPerTile;
    uint terrainEnabled;
    float terrainHeights[];
};

struct GPUVFXSpawnRequest
{
    vec3 position;
    float scale;
    vec3 direction;
    uint packedColor;
    uint seed;
    uint flags;
    uint reservedTemplate;
    uint pad;
};

layout(std430, set = 0, binding = 10) readonly buffer SpawnRequestBuffer {
    GPUVFXSpawnRequest spawnRequests[];
};

// VK-1501: GPU event->child fast path. Sibling ring to binding 10 but GPU-WRITABLE: parents append
// child spawn requests here on death/collision; a persistent child listener consumes them next frame.
// Ping-pong by frame parity keeps the writer (this frame) and reader (next frame) on disjoint halves,
// so there is no read-while-write race despite the undefined per-emitter dispatch order. Layout
// mirrors the C++ childSpawnBuffer sizing (utilities/vfx/VFXChildSpawn.hpp).
const uint CHILD_MAX_REGIONS = 64u;
const uint CHILD_MAX_REQUESTS_PER_REGION = 256u;
const uint CHILD_COUNTER_COUNT = 2u * CHILD_MAX_REGIONS;
const uint CHILD_REGION_NONE = 0xFFu;
const uint CHILD_INHERIT_COLOR_BIT = 1u << 8;
const uint CHILD_INHERIT_SIZE_BIT = 1u << 9;
const uint CHILD_INHERIT_VEL_BIT = 1u << 10;

layout(std430, set = 0, binding = 11) buffer ChildSpawnBuffer {
    uint childSpawnCounters[CHILD_COUNTER_COUNT];
    GPUVFXSpawnRequest childSpawnRequests[];
};

// VK-1502: depth-buffer collision inputs. Mirror of C++ GPUVFXCameraUBO (bound here as a read-only SSBO
// so the sim inherits the already-enabled storage-buffer UpdateAfterBind feature instead of relying on
// uniform-buffer UAB). std430 layout matches the 160-byte C++ struct exactly.
layout(std430, set = 0, binding = 12) readonly buffer CameraBuffer {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float nearPlane;
    float farPlane;
    uint depthCollisionActive; // 1 iff a valid last-frame depth is bound AND some emitter wants it
    uint prevDepthSlot;        // which prevFrameDepth[] element to sample this frame
} cam;

// Last-frame scene depth, double-buffered per frame-in-flight (MAX_FRAMES_IN_FLIGHT = 2). Populated by the
// DepthCopy pass and kept in eShaderReadOnlyOptimal; the active slot for this frame is cam.prevDepthSlot.
layout(set = 0, binding = 13) uniform sampler2D prevFrameDepth[2];

const uint COLLIDER_SPHERE  = 0u;
const uint COLLIDER_BOX     = 1u;
const uint COLLIDER_CAPSULE = 2u;
const uint MAX_SCENE_COLLIDERS = 256u;

const uint EVENT_FLAG_ON_SPAWN = 1u;
const uint EVENT_FLAG_ON_DEATH = 2u;
const uint EVENT_FLAG_ON_COLLISION = 4u;
const uint EVENT_FLAG_ON_LIFETIME_THRESHOLD = 8u;
const uint MAX_VFX_EVENTS = 256u;

const uint MAX_TRAIL_POINTS_STRIDE = 256u;
const uint RENDER_MODE_RIBBON = 4u;
const uint SPAWN_REQUEST_HAS_TINT = 1u;
const uint SPAWN_REQUEST_GPU_VALID = 2u; // VK-1501: set on GPU-appended event->child requests

// LUT_FLAG_* / LUT_CH_* / vfxNormalize01 now live in vfx_lut.glsl (shared with the ribbon shader).

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
    uint frameNumber;
    uint emitterCount;
    uint channelRequestBase;
    uint particlesPerRequest;
    uint gpuChildRegion; // VK-1501: child region for a GPU event->child listener dispatch; 0xFFFFFFFF = not a GPU child
} pc;

const uint FLAG_PLAYING = 1u;
const float FADE_START = 0.7;

uint pcg_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randomFloat(inout uint seed)
{
    seed = pcg_hash(seed);
    return float(seed) / float(0xFFFFFFFFu);
}

vec3 randomInCone(inout uint seed, vec3 baseDir, float spreadAngle)
{
    float theta = randomFloat(seed) * 6.28318530718;
    float phi = randomFloat(seed) * spreadAngle;

    vec3 up = abs(baseDir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, baseDir));
    vec3 forward = normalize(cross(baseDir, right));

    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    vec3 offset = right * (sinPhi * cos(theta)) + forward * (sinPhi * sin(theta));

    return normalize(baseDir * cosPhi + offset);
}

vec3 generateSpherePosition(inout uint seed, float radius, bool surfaceOnly)
{
    float theta = randomFloat(seed) * 6.28318530718;
    float u = randomFloat(seed) * 2.0 - 1.0;
    float phi = acos(u);

    vec3 direction;
    direction.x = sin(phi) * cos(theta);
    direction.y = cos(phi);
    direction.z = sin(phi) * sin(theta);

    float r = radius;
    if (!surfaceOnly)
    {
        r = radius * pow(randomFloat(seed), 1.0 / 3.0);
    }

    return direction * r;
}

vec3 generateConePosition(inout uint seed, float baseRadius, float height, float angle, bool surfaceOnly)
{
    float t = randomFloat(seed);

    if (!surfaceOnly)
    {
        t = sqrt(randomFloat(seed));
    }

    float y = t * height;
    float currentRadius = t * baseRadius * tan(angle);

    float theta = randomFloat(seed) * 6.28318530718;

    float r = currentRadius;
    if (!surfaceOnly)
    {
        r = currentRadius * sqrt(randomFloat(seed));
    }

    return vec3(r * cos(theta), y, r * sin(theta));
}

vec3 generateBoxPosition(inout uint seed, vec3 halfExtents, bool surfaceOnly)
{
    if (!surfaceOnly)
    {
        return vec3(
            (randomFloat(seed) * 2.0 - 1.0) * halfExtents.x,
            (randomFloat(seed) * 2.0 - 1.0) * halfExtents.y,
            (randomFloat(seed) * 2.0 - 1.0) * halfExtents.z
        );
    }

    float areaXY = halfExtents.x * halfExtents.y;
    float areaXZ = halfExtents.x * halfExtents.z;
    float areaYZ = halfExtents.y * halfExtents.z;
    float totalArea = 2.0 * (areaXY + areaXZ + areaYZ);

    float faceSelect = randomFloat(seed) * totalArea;
    float u = randomFloat(seed) * 2.0 - 1.0;
    float v = randomFloat(seed) * 2.0 - 1.0;

    if (faceSelect < areaYZ)
        return vec3(halfExtents.x, u * halfExtents.y, v * halfExtents.z);
    faceSelect -= areaYZ;

    if (faceSelect < areaYZ)
        return vec3(-halfExtents.x, u * halfExtents.y, v * halfExtents.z);
    faceSelect -= areaYZ;

    if (faceSelect < areaXZ)
        return vec3(u * halfExtents.x, halfExtents.y, v * halfExtents.z);
    faceSelect -= areaXZ;

    if (faceSelect < areaXZ)
        return vec3(u * halfExtents.x, -halfExtents.y, v * halfExtents.z);
    faceSelect -= areaXZ;

    if (faceSelect < areaXY)
        return vec3(u * halfExtents.x, v * halfExtents.y, halfExtents.z);

    return vec3(u * halfExtents.x, v * halfExtents.y, -halfExtents.z);
}

// VK-1525: real 3-D torus (major = ring radius, minor = tube radius). Mirrors the CPU
// preview VFXParticleSystem::generateTorusPosition so runtime and preview agree (the
// old flat-circle interpretation of bit 11 is gone).
vec3 generateTorusPosition3D(inout uint seed, float majorRadius, float minorRadius, bool surfaceOnly)
{
    float theta = randomFloat(seed) * 6.28318530718;
    float phi = randomFloat(seed) * 6.28318530718;

    float tubeRadius = minorRadius;
    if (!surfaceOnly)
    {
        tubeRadius = minorRadius * sqrt(randomFloat(seed));
    }

    float ringDist = majorRadius + tubeRadius * cos(phi);
    return vec3(ringDist * cos(theta), tubeRadius * sin(phi), ringDist * sin(theta));
}

// VK-1525: flat ring / arc / annulus in the XZ plane. Surface = centerline outline at
// `radius`; Volume = annular band of half-width `thickness` around it. `arcSpan` (radians)
// and `startAngle` bound the swept arc (a full 2*pi ring by default).
vec3 generateRingPosition(inout uint seed, float radius, float thickness, float arcSpan, float startAngle, bool surfaceOnly)
{
    float theta = startAngle + randomFloat(seed) * arcSpan;

    float r = radius;
    if (!surfaceOnly)
    {
        r = radius + (randomFloat(seed) * 2.0 - 1.0) * thickness;
    }

    return vec3(r * cos(theta), 0.0, r * sin(theta));
}

vec3 generateSpawnPosition(inout uint seed, GPUEmitterConfig config)
{
    bool surfaceOnly = (config.shapeFlags & SHAPE_EMIT_FROM_SURFACE) != 0u;

    if ((config.shapeFlags & SHAPE_SPHERE) != 0u)
    {
        return generateSpherePosition(seed, config.shapeDimensions.x, surfaceOnly);
    }
    else if ((config.shapeFlags & SHAPE_CONE) != 0u)
    {
        return generateConePosition(seed, config.shapeDimensions.x, config.shapeDimensions.y, config.shapeDimensions.z, surfaceOnly);
    }
    else if ((config.shapeFlags & SHAPE_BOX) != 0u)
    {
        return generateBoxPosition(seed, config.shapeDimensions.xyz, surfaceOnly);
    }
    else if ((config.shapeFlags & SHAPE_TORUS) != 0u)
    {
        return generateTorusPosition3D(seed, config.shapeDimensions.x, config.shapeDimensions.y, surfaceOnly);
    }
    else if ((config.shapeFlags & SHAPE_RING) != 0u)
    {
        return generateRingPosition(seed, config.shapeDimensions.x, config.shapeDimensions.y,
                                    config.shapeDimensions.z, config.shapeDimensions.w, surfaceOnly);
    }

    return vec3(0.0);
}

vec3 generateDirectionFromShape(inout uint seed, vec3 position, GPUEmitterConfig config)
{
    if ((config.shapeFlags & SHAPE_RANDOM_DIRECTION) != 0u)
    {
        vec3 baseDir = normalize(config.emitDirection.xyz);
        float spread = config.emitDirection.w;
        return randomInCone(seed, baseDir, spread);
    }

    if ((config.shapeFlags & SHAPE_SPHERE) != 0u)
    {
        float len = length(position);
        if (len > 0.001)
            return position / len;
        return vec3(0.0, 1.0, 0.0);
    }
    else if ((config.shapeFlags & SHAPE_CONE) != 0u)
    {
        float angle = config.shapeDimensions.z;
        vec3 radial = vec3(position.x, 0.0, position.z);
        float radialLen = length(radial);

        if (radialLen > 0.001)
        {
            vec3 outward = radial / radialLen;
            return normalize(outward * sin(angle) + vec3(0.0, cos(angle), 0.0));
        }
        return vec3(0.0, 1.0, 0.0);
    }
    else if ((config.shapeFlags & SHAPE_BOX) != 0u)
    {
        vec3 halfExtents = config.shapeDimensions.xyz;
        vec3 absPos = abs(position);
        vec3 normalizedPos = absPos / max(halfExtents, vec3(0.001));

        if (normalizedPos.x >= normalizedPos.y && normalizedPos.x >= normalizedPos.z)
            return vec3(position.x > 0.0 ? 1.0 : -1.0, 0.0, 0.0);
        else if (normalizedPos.y >= normalizedPos.x && normalizedPos.y >= normalizedPos.z)
            return vec3(0.0, position.y > 0.0 ? 1.0 : -1.0, 0.0);
        else
            return vec3(0.0, 0.0, position.z > 0.0 ? 1.0 : -1.0);
    }
    else if ((config.shapeFlags & SHAPE_TORUS) != 0u)
    {
        // Tube-outward normal (mirror of the CPU preview torus direction).
        vec3 radial = vec3(position.x, 0.0, position.z);
        float radialLen = length(radial);
        if (radialLen > 0.001)
        {
            vec3 ringPoint = (radial / radialLen) * config.shapeDimensions.x;
            vec3 tubeDir = position - ringPoint;
            float tubeLen = length(tubeDir);
            if (tubeLen > 0.001)
                return tubeDir / tubeLen;
        }
        return vec3(0.0, 1.0, 0.0);
    }
    else if ((config.shapeFlags & SHAPE_RING) != 0u)
    {
        // Radial-outward in the ring plane; upward fallback at the exact center.
        vec3 radial = vec3(position.x, 0.0, position.z);
        float radialLen = length(radial);
        if (radialLen > 0.001)
            return radial / radialLen;
        return vec3(0.0, 1.0, 0.0);
    }

    vec3 baseDir = normalize(config.emitDirection.xyz);
    float spread = config.emitDirection.w;
    return randomInCone(seed, baseDir, spread);
}

vec3 mod289_3(vec3 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 mod289_4(vec4 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 permute(vec4 x) {
    return mod289_4(((x * 34.0) + 1.0) * x);
}

vec4 taylorInvSqrt(vec4 r) {
    return 1.79284291400159 - 0.85373472095314 * r;
}

float simplexNoise3D(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod289_3(i);
    vec4 p = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0))
        + i.y + vec4(0.0, i1.y, i2.y, 1.0))
        + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);

    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);

    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// Curl noise (VK-1466). Mirror of vfx::evalCurlNoise in VFXCurlNoise.hpp.
const float CURL_EPSILON = 0.01; // finite-difference step, shared with CPU + divergence test

// fBM scalar potential at a decorrelation offset (mirrors the Turbulence octave loop).
float curlPotential(vec3 q, vec3 offset, int octaves)
{
    if (octaves <= 1)
        return simplexNoise3D(q + offset);

    float amplitude = 1.0;
    float frequency = 1.0;
    float sum = 0.0;
    for (int i = 0; i < octaves && i < 4; ++i)
    {
        sum += simplexNoise3D(q * frequency + offset) * amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return sum;
}

void applyForces(inout GPUParticle p, GPUEmitterConfig config, float time)
{
    vec3 totalForce = vec3(0.0);

    if ((config.modifierFlags & FORCE_GRAVITY) != 0u)
    {
        totalForce += config.gravityDir.xyz * config.gravityDir.w;
    }

    if ((config.modifierFlags & FORCE_WIND) != 0u)
    {
        vec3 windForce = config.windDir.xyz * config.windDir.w;

        if (config.windNoise.x > 0.0)
        {
            vec3 noisePos = p.position * config.windNoise.y + vec3(time);
            float noiseX = simplexNoise3D(noisePos);
            float noiseY = simplexNoise3D(noisePos + vec3(100.0));
            float noiseZ = simplexNoise3D(noisePos + vec3(200.0));
            windForce += vec3(noiseX, noiseY, noiseZ) * config.windNoise.x;
        }

        totalForce += windForce;
    }

    if ((config.modifierFlags & FORCE_TURBULENCE) != 0u)
    {
        vec3 noisePos = p.position * config.turbulence.y;
        noisePos += vec3(time * config.turbulence.z);

        vec3 turbForce;
        int octaves = int(config.turbulence.w);
        if (octaves <= 1)
        {
            turbForce.x = simplexNoise3D(noisePos);
            turbForce.y = simplexNoise3D(noisePos + vec3(100.0));
            turbForce.z = simplexNoise3D(noisePos + vec3(200.0));
        }
        else
        {
            float amplitude = 1.0;
            float frequency = 1.0;
            turbForce = vec3(0.0);
            for (int i = 0; i < octaves && i < 4; ++i)
            {
                vec3 samplePos = noisePos * frequency;
                turbForce.x += simplexNoise3D(samplePos) * amplitude;
                turbForce.y += simplexNoise3D(samplePos + vec3(100.0)) * amplitude;
                turbForce.z += simplexNoise3D(samplePos + vec3(200.0)) * amplitude;
                amplitude *= 0.5;
                frequency *= 2.0;
            }
        }

        totalForce += turbForce * config.turbulence.x;
    }

    if ((config.modifierFlags & FORCE_VORTEX) != 0u)
    {
        vec3 toParticle = p.position - config.vortexCenter.xyz;
        vec3 axis = normalize(config.vortexAxis.xyz);

        float axisComponent = dot(toParticle, axis);
        vec3 radial = toParticle - axis * axisComponent;
        float dist = length(radial);

        if (dist > 0.001)
        {
            vec3 tangent = normalize(cross(axis, radial));
            totalForce += tangent * config.vortexAxis.w;

            if (abs(config.vortexCenter.w) > 0.001)
            {
                vec3 radialDir = normalize(radial);
                totalForce += radialDir * config.vortexCenter.w;
            }
        }
    }

    if ((config.modifierFlags & FORCE_ATTRACTOR) != 0u)
    {
        vec3 toCenter = config.attractorParams.xyz - p.position;
        float dist = length(toCenter);
        float radius = config.dragAttractorExtra.z;
        if (dist > 1e-4 && dist < radius)
        {
            float t = clamp(1.0 - dist / radius, 0.0, 1.0);
            float falloff = pow(t, config.dragAttractorExtra.w);
            totalForce += (toCenter / dist) * config.attractorParams.w * falloff;
        }
    }

    if ((config.modifierFlags & FORCE_CURL_NOISE) != 0u)
    {
        vec3 q = p.position * config.curlNoiseParams.y + vec3(time * config.curlNoiseParams.z);
        int octaves = int(config.curlNoiseParams.w);
        float inv2h = 1.0 / (2.0 * CURL_EPSILON);
        vec3 dx = vec3(CURL_EPSILON, 0.0, 0.0);
        vec3 dy = vec3(0.0, CURL_EPSILON, 0.0);
        vec3 dz = vec3(0.0, 0.0, CURL_EPSILON);
        vec3 o1 = vec3(0.0);
        vec3 o2 = vec3(100.0);
        vec3 o3 = vec3(200.0);

        float dPsi3_dy = (curlPotential(q + dy, o3, octaves) - curlPotential(q - dy, o3, octaves)) * inv2h;
        float dPsi2_dz = (curlPotential(q + dz, o2, octaves) - curlPotential(q - dz, o2, octaves)) * inv2h;
        float dPsi1_dz = (curlPotential(q + dz, o1, octaves) - curlPotential(q - dz, o1, octaves)) * inv2h;
        float dPsi3_dx = (curlPotential(q + dx, o3, octaves) - curlPotential(q - dx, o3, octaves)) * inv2h;
        float dPsi2_dx = (curlPotential(q + dx, o2, octaves) - curlPotential(q - dx, o2, octaves)) * inv2h;
        float dPsi1_dy = (curlPotential(q + dy, o1, octaves) - curlPotential(q - dy, o1, octaves)) * inv2h;

        vec3 curlForce = vec3(dPsi3_dy - dPsi2_dz,   // curl.x
                              dPsi1_dz - dPsi3_dx,   // curl.y
                              dPsi2_dx - dPsi1_dy);  // curl.z
        totalForce += curlForce * config.curlNoiseParams.x;
    }

    p.velocity += totalForce * config.deltaTime;

    // Drag is applied multiplicatively AFTER integration so it can never reverse
    // velocity, even when (k * dt) > 1. (Explicit v -= k*v*dt would overshoot.)
    if ((config.modifierFlags & FORCE_DRAG) != 0u)
    {
        float k = config.dragAttractorExtra.x + config.dragAttractorExtra.y * length(p.velocity);
        p.velocity /= (1.0 + max(k, 0.0) * config.deltaTime);
    }
}

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

void applyModifiers(inout GPUParticle p, GPUEmitterConfig config, float lifetimeRatio)
{
    if ((config.modifierFlags & MODIFIER_COLOR_OVER_LIFETIME) != 0u)
    {
        if ((config.lutFlags & LUT_FLAG_COLOR) != 0u)
        {
            p.color = sampleLUT(config.lutBaseOffset, 0u, config.lutChannelStride, lifetimeRatio);
        }
        else
        {
            p.color = mix(config.colorStart, config.colorEnd, lifetimeRatio);
        }
        if (pc.particlesPerRequest > 0u)
        {
            uint varianceSeed = pcg_hash(p.spawnSeed);
            float colorValueMult = 1.0 + config.colorValueVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_COLOR_VALUE);
            float alphaMult = 1.0 + config.alphaVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_ALPHA);
            p.color *= vec4(colorValueMult, colorValueMult, colorValueMult, alphaMult) *
                unpackUnorm4x8(p.packedColorMult);
        }
        else
        {
            vec2 colorMult = unpackHalf2x16(p.packedColorMult);
            p.color.rgb *= colorMult.x;
            p.color.a *= colorMult.y;
        }
    }

    if ((config.modifierFlags & MODIFIER_SIZE_OVER_LIFETIME) != 0u)
    {
        float sizeMult;
        if ((config.lutFlags & LUT_FLAG_SIZE) != 0u)
        {
            sizeMult = sampleLUT(config.lutBaseOffset, 1u, config.lutChannelStride, lifetimeRatio).x;
        }
        else
        {
            sizeMult = mix(config.sizeStartMult, config.sizeEndMult, lifetimeRatio);
        }
        p.size = p.initialSize * sizeMult;
    }

    if ((config.modifierFlags & MODIFIER_SPEED_OVER_LIFETIME) != 0u)
    {
        float speedMult;
        if ((config.lutFlags & LUT_FLAG_SPEED) != 0u)
        {
            speedMult = sampleLUT(config.lutBaseOffset, 2u, config.lutChannelStride, lifetimeRatio).x;
        }
        else
        {
            speedMult = mix(config.speedStartMult, config.speedEndMult, lifetimeRatio);
        }
        float currentSpeed = length(p.velocity);
        if (currentSpeed > 0.001)
        {
            vec3 dir = p.velocity / currentSpeed;
            p.velocity = dir * p.initialSpeed * speedMult;
        }
    }

    if ((config.modifierFlags & MODIFIER_ROTATION_OVER_LIFETIME) != 0u)
    {
        if ((config.lutFlags & LUT_FLAG_ROTATION) != 0u)
        {
            float angVel = sampleLUT(config.lutBaseOffset, 3u, config.lutChannelStride, lifetimeRatio).x;
            p.rotation += angVel * config.deltaTime;
        }
        else
        {
            p.rotation += config.angularVelocity * config.deltaTime;
        }
    }

    if ((config.modifierFlags & MODIFIER_GLOW_OVER_LIFETIME) != 0u)
    {
        if ((config.lutFlags & LUT_FLAG_GLOW) != 0u)
        {
            p.glowIntensity = sampleLUT(config.lutBaseOffset, 4u, config.lutChannelStride, lifetimeRatio).x;
        }
    }

    // VK-1473 SizeBySpeed: multiply size by a curve sampled by normalized speed. Runs AFTER the
    // over-lifetime blocks and reconstructs a fresh base each frame (p.size / p.initialSize) so an
    // absent SizeOverLifetime sibling can't compound frame-over-frame.
    if ((config.modifierFlags & MODIFIER_SIZE_BY_SPEED) != 0u &&
        (config.lutFlags & LUT_FLAG_SIZE_BY_SPEED) != 0u)
    {
        float tSpeed = vfxNormalize01(length(p.velocity), config.modifierSpeedRanges.x, config.modifierSpeedRanges.y);
        float sizeBySpeed = sampleLUT(config.lutBaseOffset, LUT_CH_SIZE_BY_SPEED, config.lutChannelStride, tSpeed).x;
        float sizeBase = ((config.modifierFlags & MODIFIER_SIZE_OVER_LIFETIME) != 0u) ? p.size : p.initialSize;
        p.size = sizeBase * sizeBySpeed;
    }

    // VK-1473 ColorBySpeed: multiply color by a gradient sampled by normalized speed. Base is the
    // fresh over-lifetime color if present, else the reconstructed spawn color (matches spawn at
    // p.color = startColor * unpackHalf2x16(packedColorMult)).
    if ((config.modifierFlags & MODIFIER_COLOR_BY_SPEED) != 0u &&
        (config.lutFlags & LUT_FLAG_COLOR_BY_SPEED) != 0u)
    {
        float tSpeed = vfxNormalize01(length(p.velocity), config.modifierSpeedRanges.z, config.modifierSpeedRanges.w);
        vec4 colorBySpeed = sampleLUT(config.lutBaseOffset, LUT_CH_COLOR_BY_SPEED, config.lutChannelStride, tSpeed);
        vec4 colorBase;
        if ((config.modifierFlags & MODIFIER_COLOR_OVER_LIFETIME) != 0u)
        {
            colorBase = p.color;
        }
        else
        {
            colorBase = config.startColor;
            if (pc.particlesPerRequest > 0u)
            {
                uint varianceSeed = pcg_hash(p.spawnSeed);
                float colorValueMult = 1.0 + config.colorValueVariance *
                    vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_COLOR_VALUE);
                float alphaMult = 1.0 + config.alphaVariance *
                    vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_ALPHA);
                colorBase *= vec4(colorValueMult, colorValueMult, colorValueMult, alphaMult) *
                    unpackUnorm4x8(p.packedColorMult);
            }
            else
            {
                vec2 cm = unpackHalf2x16(p.packedColorMult);
                colorBase.rgb *= cm.x;
                colorBase.a *= cm.y;
            }
        }
        p.color = colorBase * colorBySpeed;
    }
}

// VK-1501: append one GPU event->child spawn request into this frame's write parity half of the
// given child region. Bounded by CHILD_MAX_REQUESTS_PER_REGION (overflow increments the counter but
// drops the write; the reader clamps to the same cap next frame, so overflow degrades gracefully).
void appendChildRequest(uint childHalf, vec3 pos, vec3 vel, vec3 color, float size)
{
    uint region = childHalf & CHILD_REGION_NONE;
    uint writeHalf = pc.frameNumber & 1u;
    uint base = writeHalf * CHILD_MAX_REGIONS + region;
    uint slot = atomicAdd(childSpawnCounters[base], 1u);
    if (slot < CHILD_MAX_REQUESTS_PER_REGION)
    {
        GPUVFXSpawnRequest r;
        r.position = pos;
        r.scale = ((childHalf & CHILD_INHERIT_SIZE_BIT) != 0u) ? max(size, 0.0) : 1.0;
        r.direction = ((childHalf & CHILD_INHERIT_VEL_BIT) != 0u) ? vel : vec3(0.0);
        r.packedColor = packUnorm4x8(vec4(clamp(color, 0.0, 1.0), 1.0));
        r.seed = pcg_hash(floatBitsToUint(pos.x) ^ (pc.frameNumber * 2654435761u) ^ (slot * 40503u) ^ region);
        r.flags = SPAWN_REQUEST_GPU_VALID |
                  (((childHalf & CHILD_INHERIT_COLOR_BIT) != 0u) ? SPAWN_REQUEST_HAS_TINT : 0u);
        r.reservedTemplate = 0u;
        r.pad = 0u;
        childSpawnRequests[base * CHILD_MAX_REQUESTS_PER_REGION + slot] = r;
    }
}

void emitEvent(GPUEmitterConfig config, uint type, vec3 pos, vec3 vel, uint emitterIdx, vec3 color, float size)
{
    // VK-1501: route death/collision to the GPU child ring when a fast-path child is configured. This
    // deliberately skips the CPU event buffer entirely for that pair (no readback, no notification, and
    // the 256/frame event-buffer stats stay unchanged). Non-fast-path events fall through as before.
    if (type == 1u || type == 2u)
    {
        uint childHalf = (type == 2u) ? ((config.eventChildSlot >> 16) & 0xFFFFu)
                                      : (config.eventChildSlot & 0xFFFFu);
        if ((childHalf & CHILD_REGION_NONE) != CHILD_REGION_NONE)
        {
            appendChildRequest(childHalf, pos, vel, color, size);
            return;
        }
    }

    uint idx = atomicAdd(eventCount, 1u);
    if (idx < MAX_VFX_EVENTS)
    {
        events[idx].position = pos;
        events[idx].eventType = type;
        events[idx].velocity = vel;
        events[idx].emitterIndex = emitterIdx;
        events[idx].color = color;
        events[idx].size = size;
    }
}

float sampleTerrainHeight(vec3 worldPos)
{
    float localX = worldPos.x - terrainWorldOriginX;
    float localZ = worldPos.z - terrainWorldOriginZ;

    float tileSize = terrainTileWorldSize;
    int tileX = int(floor(localX / tileSize));
    int tileZ = int(floor(localZ / tileSize));

    if (tileX < 0 || tileX >= terrainGridCountX || tileZ < 0 || tileZ >= terrainGridCountZ)
        return -1e10;

    float inTileX = localX - float(tileX) * tileSize;
    float inTileZ = localZ - float(tileZ) * tileSize;

    float spacing = terrainVertexSpacing;
    float gx = inTileX / spacing;
    float gz = inTileZ / spacing;

    uint vpt = terrainVerticesPerTile;
    int maxIdx = int(vpt) - 1;

    int ix = clamp(int(floor(gx)), 0, maxIdx - 1);
    int iz = clamp(int(floor(gz)), 0, maxIdx - 1);

    float fx = gx - float(ix);
    float fz = gz - float(iz);
    fx = clamp(fx, 0.0, 1.0);
    fz = clamp(fz, 0.0, 1.0);

    uint tileIndex = uint(tileZ) * uint(terrainGridCountX) + uint(tileX);
    uint tileOffset = tileIndex * vpt * vpt;

    float h00 = terrainHeights[tileOffset + uint(iz) * vpt + uint(ix)];
    float h10 = terrainHeights[tileOffset + uint(iz) * vpt + uint(ix + 1)];
    float h01 = terrainHeights[tileOffset + uint(iz + 1) * vpt + uint(ix)];
    float h11 = terrainHeights[tileOffset + uint(iz + 1) * vpt + uint(ix + 1)];

    float h0 = mix(h00, h10, fx);
    float h1 = mix(h01, h11, fx);
    return mix(h0, h1, fz);
}

vec3 getTerrainNormal(vec3 worldPos)
{
    float spacing = terrainVertexSpacing;
    float hL = sampleTerrainHeight(worldPos - vec3(spacing, 0.0, 0.0));
    float hR = sampleTerrainHeight(worldPos + vec3(spacing, 0.0, 0.0));
    float hD = sampleTerrainHeight(worldPos - vec3(0.0, 0.0, spacing));
    float hU = sampleTerrainHeight(worldPos + vec3(0.0, 0.0, spacing));

    vec3 normal = vec3(hL - hR, 2.0 * spacing, hD - hU);
    return normalize(normal);
}

void applyTerrainCollision(inout GPUParticle p, GPUEmitterConfig config, uint emitterIdx, inout bool collisionEventFired)
{
    if (config.terrainCollisionEnabled == 0u || terrainEnabled == 0u)
        return;

    float terrainY = sampleTerrainHeight(p.position);

    if (terrainY < -1e9)
        return;

    vec3 terrainPoint = vec3(p.position.x, terrainY, p.position.z);
    vec3 normal = getTerrainNormal(p.position);
    float penetration = dot(terrainPoint - p.position, normal);

    if (penetration > 0.0)
    {
        p.position += normal * penetration;

        float vn = dot(p.velocity, normal);
        if (vn < 0.0)
        {
            vec3 vNormal = normal * vn;
            vec3 vTangent = p.velocity - vNormal;

            p.velocity = vTangent * (1.0 - config.collisionFriction)
                       - vNormal * config.collisionBounce;
        }

        if (config.collisionLifetimeLoss > 0.0)
        {
            p.lifetime += p.maxLifetime * config.collisionLifetimeLoss;
        }

        if (!collisionEventFired && (config.eventFlags & EVENT_FLAG_ON_COLLISION) != 0u)
        {
            emitEvent(config, 2u, p.position, p.velocity, emitterIdx, p.color.rgb, p.size);
            collisionEventFired = true;
        }
    }
}

vec3 rotateByQuat(vec3 v, vec4 q)
{
    vec3 u = q.xyz;
    float s = q.w;
    return 2.0 * dot(u, v) * u
         + (s * s - dot(u, u)) * v
         + 2.0 * s * cross(u, v);
}

vec3 rotateByQuatInverse(vec3 v, vec4 q)
{
    return rotateByQuat(v, vec4(-q.xyz, q.w));
}

bool resolveCollisionSphere(vec3 particlePos, GPUCollider col, out vec3 hitNormal, out float penetration)
{
    vec3 center = col.positionAndType.xyz;
    float radius = col.dimensions.x;
    vec3 diff = particlePos - center;
    float dist = length(diff);

    if (dist < radius)
    {
        if (dist > 0.0001)
            hitNormal = diff / dist;
        else
            hitNormal = vec3(0.0, 1.0, 0.0);
        penetration = radius - dist;
        return true;
    }
    return false;
}

bool resolveCollisionBox(vec3 particlePos, GPUCollider col, out vec3 hitNormal, out float penetration)
{
    vec3 center = col.positionAndType.xyz;
    vec4 quat = col.rotation;
    vec3 halfExtents = col.dimensions.xyz;

    vec3 localPos = rotateByQuatInverse(particlePos - center, quat);

    vec3 absLocal = abs(localPos);
    if (absLocal.x > halfExtents.x || absLocal.y > halfExtents.y || absLocal.z > halfExtents.z)
        return false;

    vec3 depths = halfExtents - absLocal;
    float minDepth = depths.x;
    vec3 localNormal = vec3(sign(localPos.x), 0.0, 0.0);

    if (depths.y < minDepth)
    {
        minDepth = depths.y;
        localNormal = vec3(0.0, sign(localPos.y), 0.0);
    }
    if (depths.z < minDepth)
    {
        minDepth = depths.z;
        localNormal = vec3(0.0, 0.0, sign(localPos.z));
    }

    penetration = minDepth;
    hitNormal = rotateByQuat(localNormal, quat);
    return true;
}

bool resolveCollisionCapsule(vec3 particlePos, GPUCollider col, out vec3 hitNormal, out float penetration)
{
    vec3 center = col.positionAndType.xyz;
    vec4 quat = col.rotation;
    float radius = col.dimensions.x;
    float halfHeight = col.dimensions.y;

    vec3 axisDir = rotateByQuat(vec3(0.0, 1.0, 0.0), quat);
    vec3 diff = particlePos - center;

    float proj = dot(diff, axisDir);
    proj = clamp(proj, -halfHeight, halfHeight);
    vec3 closest = center + axisDir * proj;

    vec3 toParticle = particlePos - closest;
    float dist = length(toParticle);

    if (dist < radius)
    {
        if (dist > 0.0001)
            hitNormal = toParticle / dist;
        else
            hitNormal = vec3(0.0, 1.0, 0.0);
        penetration = radius - dist;
        return true;
    }
    return false;
}

void applyCollisions(inout GPUParticle p, GPUEmitterConfig config, uint emitterIdx, inout bool collisionEventFired)
{
    if (config.colliderCount == 0u)
        return;

    uint count = min(config.colliderCount, MAX_SCENE_COLLIDERS);

    for (uint i = 0u; i < count; ++i)
    {
        GPUCollider col = colliders[i];
        uint colType = uint(col.positionAndType.w);

        vec3 hitNormal;
        float penetration;
        bool hit = false;

        if (colType == COLLIDER_SPHERE)
            hit = resolveCollisionSphere(p.position, col, hitNormal, penetration);
        else if (colType == COLLIDER_BOX)
            hit = resolveCollisionBox(p.position, col, hitNormal, penetration);
        else if (colType == COLLIDER_CAPSULE)
            hit = resolveCollisionCapsule(p.position, col, hitNormal, penetration);

        if (hit)
        {
            p.position += hitNormal * penetration;

            float vn = dot(p.velocity, hitNormal);
            if (vn < 0.0)
            {
                vec3 vNormal = hitNormal * vn;
                vec3 vTangent = p.velocity - vNormal;

                p.velocity = vTangent * (1.0 - config.collisionFriction)
                           - vNormal * config.collisionBounce;
            }

            if (config.collisionLifetimeLoss > 0.0)
            {
                p.lifetime += p.maxLifetime * config.collisionLifetimeLoss;
            }

            if (!collisionEventFired && (config.eventFlags & EVENT_FLAG_ON_COLLISION) != 0u)
            {
                emitEvent(config, 2u, p.position, p.velocity, emitterIdx, p.color.rgb, p.size);
                collisionEventFired = true;
            }
        }
    }
}

// VK-1502: reconstruct a view-space position from a UV + hardware depth. Assumes a standard perspective
// RH_ZO projection (GLM_FORCE_DEPTH_ZERO_TO_ONE, NDC depth in [0,1], no reverse-Z). cam.projection is
// column-major, indexed [col][row].
vec3 reconstructViewPosDC(vec2 uv, float ndcZ)
{
    float viewZ = cam.projection[3][2] / (-ndcZ - cam.projection[2][2]); // negative in front of the camera
    vec2 ndcXY = uv * 2.0 - 1.0;
    float viewX = ndcXY.x * (-viewZ) / cam.projection[0][0];
    float viewY = ndcXY.y * (-viewZ) / cam.projection[1][1];
    return vec3(viewX, viewY, viewZ);
}

// VK-1502: depth-buffer collision. Projects the particle to screen, samples LAST-FRAME scene depth
// (prevFrameDepth, populated by DepthCopy and left shader-readable), and if the particle sits within a
// thickness shell behind the visible surface, applies the SAME bounce/friction/lifetime-loss response as
// the analytic/terrain colliders, using a depth-derived surface normal. The sim dispatches before the
// opaque pass, so the depth is inherently one frame behind (Niagara's model). On-screen geometry only;
// off-screen / sky / behind-camera particles fall through gracefully. Compute has no fragment derivatives,
// so the normal is built from explicit neighbor taps with textureLod (no implicit LOD in compute).
void applyDepthCollision(inout GPUParticle p, GPUEmitterConfig config, uint emitterIdx, inout bool collisionEventFired)
{
    uint slot = cam.prevDepthSlot;

    vec4 viewPos = cam.view * vec4(p.position, 1.0);
    vec4 clip = cam.projection * viewPos;
    if (clip.w <= 0.0)
        return; // behind the camera

    vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return; // off-screen: no depth to test against

    float ndcZ = textureLod(prevFrameDepth[slot], uv, 0.0).r;
    if (ndcZ >= 0.99999)
        return; // sky / cleared depth (far plane): nothing to collide with

    float surfDist = -(cam.projection[3][2] / (-ndcZ - cam.projection[2][2])); // camera->surface distance (positive)
    float partDist = -viewPos.z;                                               // camera->particle distance (positive)

    float penetration = partDist - surfDist;
    if (penetration <= 0.0 || penetration > config.depthCollisionThickness)
        return; // in front of the surface, or too far behind it (a different, occluded object)

    vec3 camFacing = normalize(cam.cameraPos - p.position);

    // Surface normal from depth derivatives: reconstruct the center + two neighbor texels and take the cross.
    vec2 texel = 1.0 / vec2(textureSize(prevFrameDepth[slot], 0));
    vec3 pC = reconstructViewPosDC(uv, ndcZ);
    vec3 pR = reconstructViewPosDC(uv + vec2(texel.x, 0.0), textureLod(prevFrameDepth[slot], uv + vec2(texel.x, 0.0), 0.0).r);
    vec3 pU = reconstructViewPosDC(uv + vec2(0.0, texel.y), textureLod(prevFrameDepth[slot], uv + vec2(0.0, texel.y), 0.0).r);
    vec3 nView = cross(pR - pC, pU - pC);
    float nlen = length(nView);
    vec3 depthNormal = camFacing;
    if (nlen > 1e-8)
    {
        nView /= nlen;
        if (nView.z < 0.0)
            nView = -nView; // face the camera (+Z in view space)
        depthNormal = normalize(transpose(mat3(cam.view)) * nView); // rigid view: inverse rotation = transpose
    }

    vec3 n = normalize(mix(camFacing, depthNormal, clamp(config.depthCollisionNormalInfluence, 0.0, 1.0)));

    // Reuse the exact analytic/terrain collision response (bounce / friction / lifetime-loss / event).
    p.position += n * penetration;

    float vn = dot(p.velocity, n);
    if (vn < 0.0)
    {
        vec3 vNormal = n * vn;
        vec3 vTangent = p.velocity - vNormal;
        p.velocity = vTangent * (1.0 - config.collisionFriction)
                   - vNormal * config.collisionBounce;
    }

    if (config.collisionLifetimeLoss > 0.0)
    {
        p.lifetime += p.maxLifetime * config.collisionLifetimeLoss;
    }

    if (!collisionEventFired && (config.eventFlags & EVENT_FLAG_ON_COLLISION) != 0u)
    {
        emitEvent(config, 2u, p.position, p.velocity, emitterIdx, p.color.rgb, p.size);
        collisionEventFired = true;
    }
}

vec3 sanitizeKillVolumeNormal(vec3 normal)
{
    float lenSq = dot(normal, normal);
    if (lenSq <= 1e-8)
        return vec3(0.0, 1.0, 0.0);
    return normal * inversesqrt(lenSq);
}

bool shouldKillByVolume(vec3 particlePos, GPUEmitterConfig config, mat4 worldTransform)
{
    uint packed = uint(config.killVolumeParams1.w + 0.5);
    uint shape = packed & 3u;
    bool invert = (packed & 4u) != 0u;
    bool localSpace = (packed & 8u) != 0u;

    vec3 pos = particlePos;
    if (localSpace)
        pos = (inverse(worldTransform) * vec4(particlePos, 1.0)).xyz;

    vec3 center = config.killVolumeParams0.xyz;
    bool killed = false;

    if (shape == 0u)
    {
        vec3 normal = sanitizeKillVolumeNormal(config.killVolumeParams1.xyz);
        killed = dot(normal, pos - center) < 0.0;
    }
    else if (shape == 1u)
    {
        float radius = max(config.killVolumeParams0.w, 0.0);
        vec3 delta = pos - center;
        killed = dot(delta, delta) <= radius * radius;
    }
    else if (shape == 2u)
    {
        vec3 halfExtents = max(config.killVolumeParams1.xyz, vec3(0.0));
        vec3 delta = abs(pos - center);
        killed = all(lessThanEqual(delta, halfExtents));
    }

    return invert ? !killed : killed;
}

void main()
{
    uint localIdx = gl_GlobalInvocationID.x;

    if (pc.emitterIndex >= pc.emitterCount)
        return;

    GPUEmitterConfig config = configs[pc.emitterIndex];

    uint maxParts = states[pc.emitterIndex].maxParticles;
    if (localIdx >= maxParts)
        return;

    uint particleOffset = states[pc.emitterIndex].particleOffset;
    uint spawnThisFrame = states[pc.emitterIndex].spawnThisFrame;
    mat4 worldTransform = states[pc.emitterIndex].worldTransform;
    uint emitterFlags = states[pc.emitterIndex].flags;

    bool isPlaying = (emitterFlags & FLAG_PLAYING) != 0u;

    uint particleIdx = particleOffset + localIdx;
    GPUParticle p = particles[particleIdx];

    uint seed = pcg_hash(particleIdx ^ (pc.frameNumber * 1000000u) ^ config.seed);

    bool wasActive = (p.size > 0.0);
    bool isActive = wasActive;

    if (wasActive)
    {
        p.lifetime += config.deltaTime;

        if (p.lifetime >= p.maxLifetime)
        {
            if ((config.eventFlags & EVENT_FLAG_ON_DEATH) != 0u)
            {
                emitEvent(config, 1u, p.position, p.velocity, pc.emitterIndex, p.color.rgb, p.size);
            }
            p.size = 0.0;
            isActive = false;
        }
        else
        {
            float time = float(pc.frameNumber) * 0.016;
            applyForces(p, config, time);

            p.position += p.velocity * config.deltaTime;
            p.rotation += p.angularVelocity * config.deltaTime;

            bool collisionEventFired = false;
            applyCollisions(p, config, pc.emitterIndex, collisionEventFired);
            applyTerrainCollision(p, config, pc.emitterIndex, collisionEventFired);
            if ((config.modifierFlags & MODIFIER_DEPTH_COLLISION) != 0u && cam.depthCollisionActive != 0u)
            {
                applyDepthCollision(p, config, pc.emitterIndex, collisionEventFired);
            }

            float lifetimeRatio = p.lifetime / p.maxLifetime;

            // Exclude the depth-collision bit from the "has any modifier" gate: it is a collision toggle, not
            // a modifier, and a depth-collision-only emitter must still take the legacy alpha-fade path below.
            if ((config.modifierFlags & ~MODIFIER_DEPTH_COLLISION) != 0u)
            {
                applyModifiers(p, config, lifetimeRatio);
            }
            else
            {
                if (lifetimeRatio > FADE_START)
                {
                    float fadeProgress = (lifetimeRatio - FADE_START) / (1.0 - FADE_START);
                    float alphaMult = (pc.particlesPerRequest > 0u)
                        ? (1.0 + config.alphaVariance * vfxVarianceSigned(
                              pcg_hash(p.spawnSeed), VFX_VAR_STREAM_ALPHA)) *
                          unpackUnorm4x8(p.packedColorMult).a
                        : unpackHalf2x16(p.packedColorMult).y;
                    p.color.a = config.startColor.a * alphaMult * (1.0 - fadeProgress);
                }
            }

            if ((config.eventFlags & EVENT_FLAG_ON_LIFETIME_THRESHOLD) != 0u)
            {
                float prevRatio = (p.lifetime - config.deltaTime) / p.maxLifetime;
                if (prevRatio < config.lifetimeThreshold && lifetimeRatio >= config.lifetimeThreshold)
                {
                    emitEvent(config, 3u, p.position, p.velocity, pc.emitterIndex, p.color.rgb, p.size);
                }
            }

            // Kill-at-center: run after modifiers so a SizeOverLifetime modifier can't
            // resurrect size. isActive gates the activeCount increment below.
            if (isActive &&
                (config.modifierFlags & FORCE_ATTRACTOR) != 0u &&
                (config.modifierFlags & FORCE_ATTRACTOR_KILL) != 0u)
            {
                float killRadius = max(0.05, config.dragAttractorExtra.z * 0.05);
                if (distance(p.position, config.attractorParams.xyz) < killRadius)
                {
                    if ((config.eventFlags & EVENT_FLAG_ON_DEATH) != 0u)
                    {
                        emitEvent(config, 1u, p.position, p.velocity, pc.emitterIndex, p.color.rgb, p.size);
                    }
                    p.size = 0.0;
                    isActive = false;
                }
            }

            if (isActive &&
                (config.modifierFlags & FORCE_KILL_VOLUME) != 0u &&
                shouldKillByVolume(p.position, config, worldTransform))
            {
                if ((config.eventFlags & EVENT_FLAG_ON_DEATH) != 0u)
                {
                    emitEvent(config, 1u, p.position, p.velocity, pc.emitterIndex, p.color.rgb, p.size);
                }
                p.size = 0.0;
                isActive = false;
            }
        }
    }

    if (isPlaying && !isActive && spawnThisFrame > 0u)
    {
        uint spawnSlot = atomicAdd(states[pc.emitterIndex].spawnCounter, 1u);

        if (spawnSlot < spawnThisFrame)
        {
            bool channelSpawn = pc.particlesPerRequest > 0u;
            bool gpuChildSpawn = pc.gpuChildRegion != 0xFFFFFFFFu; // VK-1501
            bool useRequest = channelSpawn || gpuChildSpawn;
            bool requestValid = true;
            float requestScale = 1.0;
            vec3 requestDirection = vec3(0.0);
            uint requestPackedColor = 0u;
            uint requestFlags = 0u;

            if (gpuChildSpawn)
            {
                // VK-1501: consume GPU-appended child requests from the previous-frame parity half.
                // The count is GPU-authoritative (clamped to the per-region cap) so no CPU readback is
                // ever needed; the read half has no concurrent writer this frame (plain loads are safe).
                uint readHalf = (pc.frameNumber & 1u) ^ 1u;
                uint base = readHalf * CHILD_MAX_REGIONS + pc.gpuChildRegion;
                uint count = min(childSpawnCounters[base], CHILD_MAX_REQUESTS_PER_REGION);
                uint requestOffset = spawnSlot / pc.particlesPerRequest;
                if (requestOffset >= count)
                {
                    requestValid = false;
                }
                else
                {
                    GPUVFXSpawnRequest request = childSpawnRequests[base * CHILD_MAX_REQUESTS_PER_REGION + requestOffset];
                    uint subParticleIndex = spawnSlot % pc.particlesPerRequest;
                    seed = pcg_hash(request.seed ^ pcg_hash(subParticleIndex + 0x9E3779B9u));
                    requestScale = max(request.scale, 0.0);
                    requestDirection = request.direction;
                    requestPackedColor = request.packedColor;
                    requestFlags = request.flags;
                    p.position = request.position;
                }
            }
            else if (channelSpawn)
            {
                uint requestOffset = spawnSlot / pc.particlesPerRequest;
                uint requestCapacity = uint(spawnRequests.length());
                if (pc.channelRequestBase >= requestCapacity)
                {
                    requestValid = false;
                }
                else if (requestOffset >= requestCapacity - pc.channelRequestBase)
                {
                    requestValid = false;
                }
                else
                {
                    uint requestIndex = pc.channelRequestBase + requestOffset;
                    GPUVFXSpawnRequest request = spawnRequests[requestIndex];
                    uint subParticleIndex = spawnSlot % pc.particlesPerRequest;
                    seed = pcg_hash(request.seed ^ pcg_hash(subParticleIndex + 0x9E3779B9u));
                    requestScale = max(request.scale, 0.0);
                    requestDirection = request.direction;
                    requestPackedColor = request.packedColor;
                    requestFlags = request.flags;
                    p.position = request.position;
                }
            }

            if (requestValid)
            {
                isActive = true;

                // Sub-frame fraction across this frame's spawn batch. Drives both the
                // ordered-placement sweep and the emitter-origin de-clumping below.
                float spawnFrac = (spawnThisFrame > 1u)
                    ? float(spawnSlot) / float(spawnThisFrame - 1u)
                    : 1.0;

                // VK-1525: ordered / path-driven placement (opt-in, normal emitter spawns only).
                bool orderedSpawn = !useRequest && (config.shapeFlags & SHAPE_ORDERED) != 0u;

                vec3 localPos;
                if (orderedSpawn)
                {
                    // Place along the shape by emitter age (progress), not randomly. progress
                    // interpolates the sub-frame batch across the [prev, curr] sweep t. The jitter
                    // seed is frame-independent (age + slot, not frameNumber) so seek/prewarm replay.
                    float progress = mix(config.orderedSweepTPrev, config.orderedSweepT, spawnFrac);
                    uint jitterSeed = pcg_hash(config.seed ^ pcg_hash(uint(progress * 65535.0) ^ spawnSlot));
                    localPos = vfxspOrderedPosition(config.shapeFlags, config.shapeDimensions,
                                                    progress, config.orderedJitter, jitterSeed);
                }
                else
                {
                    localPos = generateSpawnPosition(seed, config);
                }

                vec3 spawnOrigin;
                if (useRequest)
                {
                    spawnOrigin = p.position;
                    localPos *= requestScale;
                }
                else
                {
                    // Sub-frame interpolation: distribute this frame's spawns along the
                    // emitter's path from last frame to avoid beads-on-a-string clumping.
                    spawnOrigin = mix(vec3(states[pc.emitterIndex].prevWorldTransform[3]),
                                      vec3(worldTransform[3]), spawnFrac);
                }

            mat3 rotation = mat3(worldTransform);
            p.position = spawnOrigin + rotation * localPos;

            p.lifetime = 0.0;
            p.spawnSeed = seed;
            p.glowIntensity = 0.0;
            uint varianceSeed = p.spawnSeed;
            float sizeMult = max(0.0, 1.0 + config.sizeVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_SIZE));
            float lifetimeMult = max(0.01, 1.0 + config.lifetimeVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_LIFETIME));
            float speedMult = max(0.0, 1.0 + config.speedVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_SPEED));
            float colorValueMult = 1.0 + config.colorValueVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_COLOR_VALUE);
            float alphaMult = 1.0 + config.alphaVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_ALPHA);

            p.maxLifetime = config.lifetime * lifetimeMult;
            p.size = config.startSize * sizeMult * requestScale;
            p.color = config.startColor;
            p.color.rgb *= colorValueMult;
            p.color.a *= alphaMult;
            vec4 channelTint = vec4(1.0);
            if (useRequest && (requestFlags & SPAWN_REQUEST_HAS_TINT) != 0u)
            {
                channelTint = unpackUnorm4x8(requestPackedColor);
                p.color *= channelTint;
            }
            p.rotation = config.rotationVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_ROTATION);
            p.angularVelocity = config.angularVelocityVariance *
                vfxVarianceSigned(varianceSeed, VFX_VAR_STREAM_ANGULAR_VELOCITY);
            p.packedColorMult = useRequest
                ? packUnorm4x8(channelTint)
                : packHalf2x16(vec2(colorValueMult, alphaMult));

            p.initialSize = p.size;
            p.initialSpeed = config.startSpeed * speedMult;

            vec3 dir = generateDirectionFromShape(seed, localPos, config);
            bool hasWorldDirection = useRequest && dot(requestDirection, requestDirection) > 1e-8;
            if (hasWorldDirection)
            {
                dir = normalize(requestDirection);
            }
            p.velocity = dir * p.initialSpeed;

            if (!hasWorldDirection)
            {
                p.velocity = rotation * p.velocity;
            }

            vec4 emitterVel = states[pc.emitterIndex].emitterVelocityAndInherit;
            p.velocity += emitterVel.xyz * emitterVel.w;

            if (config.renderMode == RENDER_MODE_RIBBON && config.maxTrailPoints > 0u) {
                uint head = atomicAdd(ribbonHeads[pc.emitterIndex], 1u);
                uint slot = head % config.maxTrailPoints;
                ribbonRing[pc.emitterIndex * MAX_TRAIL_POINTS_STRIDE + slot] = particleIdx;
            }

            if ((config.eventFlags & EVENT_FLAG_ON_SPAWN) != 0u)
            {
                emitEvent(config, 0u, p.position, p.velocity, pc.emitterIndex, p.color.rgb, p.size);
            }
            }
        }
    }

    particles[particleIdx] = p;

    if (isActive)
    {
        atomicAdd(states[pc.emitterIndex].activeCount, 1u);
    }

    if (gl_GlobalInvocationID.x == 0u)
    {
        if (config.renderMode == RENDER_MODE_RIBBON && config.maxTrailPoints > 0u)
        {
            // Ribbon: each instance = one quad segment between two trail points
            uint head = ribbonHeads[pc.emitterIndex];
            uint usedPoints = min(head, config.maxTrailPoints);
            uint segments = (usedPoints > 1u) ? (usedPoints - 1u) : 0u;
            drawCommands[pc.emitterIndex].indexCount = 6u;
            drawCommands[pc.emitterIndex].instanceCount = segments;
            drawCommands[pc.emitterIndex].firstIndex = 0u;
            drawCommands[pc.emitterIndex].vertexOffset = 0;
            drawCommands[pc.emitterIndex].firstInstance = 0u;
        }
        else
        {
            drawCommands[pc.emitterIndex].indexCount = configs[pc.emitterIndex].drawIndexCount;
            drawCommands[pc.emitterIndex].instanceCount = maxParts;
            drawCommands[pc.emitterIndex].firstIndex = 0u;
            drawCommands[pc.emitterIndex].vertexOffset = 0;
            drawCommands[pc.emitterIndex].firstInstance = particleOffset;
        }
    }
}
