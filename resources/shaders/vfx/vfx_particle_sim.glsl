#type COMPUTE
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct GPUParticle
{
    vec3 position;
    float lifetime;
    vec3 velocity;
    float maxLifetime;
    vec4 color;
    float size;
    float rotation;
    float initialSize;
    float initialSpeed;
    uint spawnSeed;
    float glowIntensity;
    float _pad2;
    float _pad3;
};

const uint MODIFIER_COLOR_OVER_LIFETIME = 1u;
const uint MODIFIER_SIZE_OVER_LIFETIME = 2u;
const uint MODIFIER_SPEED_OVER_LIFETIME = 4u;
const uint MODIFIER_ROTATION_OVER_LIFETIME = 8u;

const uint FORCE_GRAVITY = 16u;
const uint FORCE_WIND = 32u;
const uint FORCE_TURBULENCE = 64u;
const uint FORCE_VORTEX = 128u;

const uint SHAPE_SPHERE = 256u;
const uint SHAPE_CONE = 512u;
const uint SHAPE_BOX = 1024u;
const uint SHAPE_CIRCLE = 2048u;
const uint SHAPE_EMIT_FROM_SURFACE = 4096u;
const uint SHAPE_RANDOM_DIRECTION = 8192u;

const uint FLIPBOOK_RANDOM_START = 16384u;
const uint MODIFIER_GLOW_OVER_LIFETIME = 32768u;

struct GPUEmitterConfig
{
    vec4 emitDirection;
    vec4 startColor;
    float spawnRate;
    float lifetime;
    float startSize;
    float startSpeed;
    uint maxParticles;
    uint seed;
    float deltaTime;
    uint modifierFlags;

    vec4 colorStart;
    vec4 colorEnd;
    float sizeStartMult;
    float sizeEndMult;
    float speedStartMult;
    float speedEndMult;
    float angularVelocity;
    uint lutBaseOffset;
    uint lutChannelStride;
    uint lutFlags;

    vec4 gravityDir;
    vec4 windDir;
    vec4 windNoise;
    vec4 turbulence;
    vec4 vortexAxis;
    vec4 vortexCenter;

    vec4 shapeDimensions;
    uint shapeFlags;
    float flipbookColumns;
    float flipbookRows;
    float flipbookFrameRate;

    uint renderMode;
    float softParticleDistance;
    float stretchMultiplier;
    uint drawIndexCount;

    uint maxTrailPoints;
    float ribbonWidth;
    float ribbonMinDistance;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    uint eventFlags;
    float lifetimeThreshold;
    uint colliderCount;
    float collisionBounce;
    float collisionFriction;
    float collisionLifetimeLoss;
    uint terrainCollisionEnabled;
};

struct GPUEmitterState
{
    mat4 worldTransform;
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

const uint LUT_FLAG_COLOR = 1u;
const uint LUT_FLAG_SIZE = 2u;
const uint LUT_FLAG_SPEED = 4u;
const uint LUT_FLAG_ROTATION = 8u;
const uint LUT_FLAG_GLOW = 16u;

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
    uint frameNumber;
    uint emitterCount;
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

vec3 generateCirclePosition(inout uint seed, float radius, float arc, bool surfaceOnly)
{
    float theta = randomFloat(seed) * arc;

    if (surfaceOnly)
    {
        return vec3(radius * cos(theta), 0.0, radius * sin(theta));
    }

    float r = radius * sqrt(randomFloat(seed));
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
    else if ((config.shapeFlags & SHAPE_CIRCLE) != 0u)
    {
        return generateCirclePosition(seed, config.shapeDimensions.x, config.shapeDimensions.y, surfaceOnly);
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
    else if ((config.shapeFlags & SHAPE_CIRCLE) != 0u)
    {
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

    p.velocity += totalForce * config.deltaTime;
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
}

void emitEvent(uint type, vec3 pos, vec3 vel, uint emitterIdx)
{
    uint idx = atomicAdd(eventCount, 1u);
    if (idx < MAX_VFX_EVENTS)
    {
        events[idx].position = pos;
        events[idx].eventType = type;
        events[idx].velocity = vel;
        events[idx].emitterIndex = emitterIdx;
    }
}

// --- Terrain heightfield sampling ---
float sampleTerrainHeight(vec3 worldPos)
{
    // Convert world position to terrain-local coordinates
    float localX = worldPos.x - terrainWorldOriginX;
    float localZ = worldPos.z - terrainWorldOriginZ;

    // Which tile are we in?
    float tileSize = terrainTileWorldSize;
    int tileX = int(floor(localX / tileSize));
    int tileZ = int(floor(localZ / tileSize));

    // Bounds check
    if (tileX < 0 || tileX >= terrainGridCountX || tileZ < 0 || tileZ >= terrainGridCountZ)
        return -1e10;

    // Position within the tile [0, tileSize]
    float inTileX = localX - float(tileX) * tileSize;
    float inTileZ = localZ - float(tileZ) * tileSize;

    // Convert to grid coordinates
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

    // Tile offset in the heights array
    uint tileIndex = uint(tileZ) * uint(terrainGridCountX) + uint(tileX);
    uint tileOffset = tileIndex * vpt * vpt;

    // Bilinear sample
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

    // No terrain at this location
    if (terrainY < -1e9)
        return;

    // Compute penetration along surface normal (not vertical)
    vec3 terrainPoint = vec3(p.position.x, terrainY, p.position.z);
    vec3 normal = getTerrainNormal(p.position);
    float penetration = dot(terrainPoint - p.position, normal);

    if (penetration > 0.0)
    {
        // Push particle out of terrain
        p.position += normal * penetration;

        // Reflect velocity
        float vn = dot(p.velocity, normal);
        if (vn < 0.0)
        {
            vec3 vNormal = normal * vn;
            vec3 vTangent = p.velocity - vNormal;

            p.velocity = vTangent * (1.0 - config.collisionFriction)
                       - vNormal * config.collisionBounce;
        }

        // Lifetime loss
        if (config.collisionLifetimeLoss > 0.0)
        {
            p.lifetime += p.maxLifetime * config.collisionLifetimeLoss;
        }

        // OnCollision event (once per particle per frame)
        if (!collisionEventFired && (config.eventFlags & EVENT_FLAG_ON_COLLISION) != 0u)
        {
            emitEvent(2u, p.position, p.velocity, emitterIdx);
            collisionEventFired = true;
        }
    }
}

// --- Quaternion helpers ---
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

// --- Collision detection per shape ---
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

    // Transform particle to collider local space
    vec3 localPos = rotateByQuatInverse(particlePos - center, quat);

    // AABB check in local space
    vec3 absLocal = abs(localPos);
    if (absLocal.x > halfExtents.x || absLocal.y > halfExtents.y || absLocal.z > halfExtents.z)
        return false;

    // Find closest face (minimum penetration axis)
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
                emitEvent(2u, p.position, p.velocity, emitterIdx);
                collisionEventFired = true;
            }
        }
    }
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
                emitEvent(1u, p.position, p.velocity, pc.emitterIndex);
            }
            p.size = 0.0;
            isActive = false;
        }
        else
        {
            float time = float(pc.frameNumber) * 0.016;
            applyForces(p, config, time);

            p.position += p.velocity * config.deltaTime;

            bool collisionEventFired = false;
            applyCollisions(p, config, pc.emitterIndex, collisionEventFired);
            applyTerrainCollision(p, config, pc.emitterIndex, collisionEventFired);

            float lifetimeRatio = p.lifetime / p.maxLifetime;

            if (config.modifierFlags != 0u)
            {
                applyModifiers(p, config, lifetimeRatio);
            }
            else
            {
                if (lifetimeRatio > FADE_START)
                {
                    float fadeProgress = (lifetimeRatio - FADE_START) / (1.0 - FADE_START);
                    p.color.a = config.startColor.a * (1.0 - fadeProgress);
                }
            }

            if ((config.eventFlags & EVENT_FLAG_ON_LIFETIME_THRESHOLD) != 0u)
            {
                float prevRatio = (p.lifetime - config.deltaTime) / p.maxLifetime;
                if (prevRatio < config.lifetimeThreshold && lifetimeRatio >= config.lifetimeThreshold)
                {
                    emitEvent(3u, p.position, p.velocity, pc.emitterIndex);
                }
            }
        }
    }

    if (isPlaying && !isActive && spawnThisFrame > 0u)
    {
        uint spawnSlot = atomicAdd(states[pc.emitterIndex].spawnCounter, 1u);

        if (spawnSlot < spawnThisFrame)
        {
            isActive = true;

            vec3 localPos = generateSpawnPosition(seed, config);

            mat3 rotation = mat3(worldTransform);
            p.position = vec3(worldTransform[3]) + rotation * localPos;

            p.lifetime = 0.0;
            p.maxLifetime = config.lifetime;
            p.size = config.startSize;
            p.color = config.startColor;
            p.rotation = 0.0;

            p.initialSize = config.startSize;
            p.initialSpeed = config.startSpeed;
            p.spawnSeed = seed;
            p.glowIntensity = 0.0;

            vec3 dir = generateDirectionFromShape(seed, localPos, config);
            p.velocity = dir * config.startSpeed;

            p.velocity = rotation * p.velocity;

            if (config.renderMode == RENDER_MODE_RIBBON && config.maxTrailPoints > 0u) {
                uint head = atomicAdd(ribbonHeads[pc.emitterIndex], 1u);
                uint slot = head % config.maxTrailPoints;
                ribbonRing[pc.emitterIndex * MAX_TRAIL_POINTS_STRIDE + slot] = particleIdx;
            }

            if ((config.eventFlags & EVENT_FLAG_ON_SPAWN) != 0u)
            {
                emitEvent(0u, p.position, p.velocity, pc.emitterIndex);
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
