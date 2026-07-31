#type COMPUTE
#version 450

// VK-1616 - hydraulic erosion brush, virtual pipe model (Mei/Decaudin/Hu, PG 2007).
//
// GPU twin of VFEngine/utilities/terrain/TerrainHydraulicErosion.hpp. Keep both in sync - there is
// no codegen between them, and test_terrain_hydraulic_erosion.cpp pins the load-bearing expressions
// below as source text precisely because a silent divergence here is invisible until someone
// notices the terrain looks wrong.
//
// Unlike brush_compute.glsl this is NOT a per-tile kernel. Water has to cross tile seams, so the
// caller gathers one brush-centred rect in global vertex space spanning however many tiles the
// brush touches, and scatters the result back to every owning tile slot afterwards.
//
// One entry point, one pipeline; `pc.passIndex` selects the pass. The branch is uniform across the
// whole dispatch (it is a push constant), so it costs nothing, and it keeps this to a single
// descriptor layout instead of six.
//
// Iteration = FLUX -> WATER -> EROSION -> ADVECT, plus THERMAL every Nth step. RESOLVE runs once at
// the end.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Pre-simulation heights. Never written by any pass; RESOLVE blends against it so that outside the
// brush radius the output is bit-exactly the input.
layout(std430, set = 0, binding = 0) readonly buffer HeightOrig { float bOrig[]; };

// Final result, read back by the CPU. Only RESOLVE writes it.
layout(std430, set = 0, binding = 1) writeonly buffer HeightOut { float heightOut[]; };

// Working terrain, 2 * cellCount. Ping-ponged because THERMAL reads 8 neighbours of a buffer it
// writes; every other pass that touches it is cell-local.
layout(std430, set = 0, binding = 2) buffer Terrain { float b[]; };

layout(std430, set = 0, binding = 3) buffer Water { float d[]; };

// Suspended sediment, 2 * cellCount. Ping-ponged because ADVECT backtraces into its neighbours.
layout(std430, set = 0, binding = 4) buffer Sediment { float s[]; };

// Outflow flux per cell: x = left (-X), y = right (+X), z = top (-Z), w = bottom (+Z).
layout(std430, set = 0, binding = 5) buffer Flux { vec4 flux[]; };

// x = u (velocity X), y = w (velocity Z), z = sin(tilt angle) cached by WATER, w = unused.
layout(std430, set = 0, binding = 6) buffer Flow { vec4 flow[]; };

// 1 where a tile owns this cell, 0 where the grid has a hole. Zero cells are walls.
layout(std430, set = 0, binding = 7) readonly buffer Valid { uint validMask[]; };

layout(push_constant) uniform PushConstants
{
    vec2 regionOriginWorld;   //   0  world XZ of region cell (0,0)
    vec2 brushCenter;         //   8
    uint regionWidth;         //  16
    uint regionHeight;        //  20
    uint passIndex;           //  24  0=FLUX 1=WATER 2=EROSION 3=ADVECT 4=THERMAL 5=RESOLVE
    uint falloffType;         //  28
    uint shapeType;           //  32
    uint terrainParity;       //  36  which half of b[] is current
    uint sedimentParity;      //  40  which half of s[] is current
    uint cellCount;           //  44  stride between the ping-pong halves
    float cellSize;           //  48  vertex spacing; lX == lZ == pipe length l
    float brushRadius;        //  52
    float dt;                 //  56  CFL-sized on the CPU; never a user slider
    float rainAmount;         //  60  metres of water per iteration at full influence
    float sedimentCapacity;   //  64  Kc
    float dissolveRate;       //  68  Ks
    float depositRate;        //  72  Kd
    float evaporation;        //  76  Ke, a per-iteration fraction (not a rate * dt)
    float gravity;            //  80  g
    float minTiltSin;         //  84
    float maxVelocity;        //  88  CFL bound on |v|
    float maxStepDelta;       //  92  ceiling on |db| for one step
    float minWater;           //  96
    float smoothing;          // 100
    float talusThreshold;     // 104  tan(radians(talusAngle)) * cellSize
    float strengthScale;      // 108
    float minHeight;          // 112
    float maxHeight;          // 116
} pc;                         // 120 bytes total

// Mirrors terrain::applyFalloff (utilities/terrain/BrushFalloff.hpp:12-22) and the identical copy
// in brush_compute.glsl:47-57. Duplicated rather than shared through an include, which is the
// convention BrushFalloff.hpp's own header comment establishes.
float applyFalloff(float t, uint type)
{
    switch (type)
    {
        case 0: return 1.0;                           // Constant
        case 1: return 1.0 - t;                       // Linear
        case 2: return 1.0 - t * t * (3.0 - 2.0 * t); // Smooth
        case 3: return 1.0 - t * t;                   // Sharp
        default: return 0.0;
    }
}

uint cellIndex(uint x, uint z)
{
    return z * pc.regionWidth + x;
}

bool inRect(ivec2 p)
{
    return p.x >= 0 && p.y >= 0 && p.x < int(pc.regionWidth) && p.y < int(pc.regionHeight);
}

bool cellValid(ivec2 p)
{
    if (!inRect(p))
        return false;
    return validMask[cellIndex(uint(p.x), uint(p.y))] != 0u;
}

uint pingPongSlot(uint idx, uint parity)
{
    return parity * pc.cellCount + idx;
}

// Brush influence at a region cell. Mirrors terrain::hydraulicInfluence.
float influenceAt(ivec2 p)
{
    if (pc.brushRadius <= 0.0)
        return 0.0;

    vec2 worldPos = pc.regionOriginWorld + vec2(float(p.x), float(p.y)) * pc.cellSize;
    vec2 delta = worldPos - pc.brushCenter;

    float dist = (pc.shapeType == 0)
        ? length(delta) / pc.brushRadius
        : max(abs(delta.x), abs(delta.y)) / pc.brushRadius;

    if (dist >= 1.0)
        return 0.0;

    return clamp(applyFalloff(dist, pc.falloffType), 0.0, 1.0);
}

// Rain is a closed-form function of position, which is exactly what lets the rain step fuse into
// FLUX: a neighbour's post-rain depth is recomputable from a single water read, with no extra pass
// and no extra barrier.
float rainAt(ivec2 p)
{
    return pc.rainAmount * influenceAt(p);
}

float surfaceWithRain(ivec2 p)
{
    uint idx = cellIndex(uint(p.x), uint(p.y));
    return b[pingPongSlot(idx, pc.terrainParity)] + d[idx] + rainAt(p);
}

// ---------------------------------------------------------------------------------------
// Pass 0 - rain, outflow flux, K limiter
// ---------------------------------------------------------------------------------------
void passFlux(ivec2 p, uint idx)
{
    float rain = rainAt(p);
    float d1 = d[idx] + rain;
    float surface = b[pingPongSlot(idx, pc.terrainParity)] + d1;

    // A = cellSize^2 (pipe cross-section), l = cellSize (pipe length), so A*g/l collapses to
    // cellSize*g.
    float accel = pc.dt * pc.cellSize * pc.gravity;

    vec4 f = flux[idx];
    ivec2 neighbours[4] = ivec2[4](p + ivec2(-1, 0), p + ivec2(1, 0),
                                   p + ivec2(0, -1), p + ivec2(0, 1));

    for (int i = 0; i < 4; ++i)
    {
        if (!cellValid(neighbours[i]))
        {
            // Two different non-cells, two different rules. A cell inside the rect that no tile
            // owns is a WALL: no outflow. The rect's own rim is ABSORBING - the neighbour's ground
            // reads as level with ours, so dh collapses to d1 and water runs off the edge. The
            // paper's no-slip rim would pond water there instead and, because a wall has no slope,
            // ring the brush with deposited sediment.
            f[i] = inRect(neighbours[i]) ? 0.0 : max(0.0, f[i] + accel * d1);
            continue;
        }
        float dh = surface - surfaceWithRain(neighbours[i]);
        f[i] = max(0.0, f[i] + accel * dh);
    }

    float total = f.x + f.y + f.z + f.w;
    if (total > 0.0)
    {
        // The paper's K limiter. Without it a cell can send away more water than it holds, d goes
        // negative, and the division by dBar in WATER produces an inf that never washes out.
        float cellArea = pc.cellSize * pc.cellSize;
        float k = min(1.0, (d1 * cellArea) / (total * pc.dt));
        f *= k;
    }
    flux[idx] = f;
}

// ---------------------------------------------------------------------------------------
// Pass 1 - water depth, velocity, and the cached tilt
// ---------------------------------------------------------------------------------------
//
// sin(alpha) is computed HERE, not in EROSION where it is used. The tilt is a gradient of the
// terrain over the neighbours, and EROSION writes terrain; reading neighbours of a buffer the same
// pass writes is a race whose symptom is non-deterministic speckle that reads as "noisy erosion"
// and gets misdiagnosed as a tuning problem. Nothing writes terrain in this pass, so here it is
// exact - and it is what lets EROSION stay cell-local and skip a ping-pong.
void passWater(ivec2 p, uint idx)
{
    float cellArea = pc.cellSize * pc.cellSize;
    float d1 = d[idx] + rainAt(p);
    vec4 f = flux[idx];

    ivec2 left = p + ivec2(-1, 0);
    ivec2 right = p + ivec2(1, 0);
    ivec2 top = p + ivec2(0, -1);
    ivec2 bottom = p + ivec2(0, 1);

    // Inflow is the neighbour's outflow pointed at us.
    float inLeft = cellValid(left) ? flux[cellIndex(uint(left.x), uint(left.y))].y : 0.0;
    float inRight = cellValid(right) ? flux[cellIndex(uint(right.x), uint(right.y))].x : 0.0;
    float inTop = cellValid(top) ? flux[cellIndex(uint(top.x), uint(top.y))].w : 0.0;
    float inBottom = cellValid(bottom) ? flux[cellIndex(uint(bottom.x), uint(bottom.y))].z : 0.0;

    float outTotal = f.x + f.y + f.z + f.w;
    float deltaVolume = pc.dt * (inLeft + inRight + inTop + inBottom - outTotal);
    float d2 = max(0.0, d1 + deltaVolume / cellArea);

    float dBar = 0.5 * (d1 + d2);
    float u = 0.0;
    float w = 0.0;
    if (dBar > pc.minWater)
    {
        float deltaWX = 0.5 * (inLeft - f.x + f.y - inRight);
        float deltaWZ = 0.5 * (inTop - f.z + f.w - inBottom);
        // The CFL bound, enforced where it actually matters: the advection backtrace must not skip
        // a cell. |u| is emergent, so it cannot be bounded a priori by choosing dt.
        u = clamp(deltaWX / (pc.cellSize * dBar), -pc.maxVelocity, pc.maxVelocity);
        w = clamp(deltaWZ / (pc.cellSize * dBar), -pc.maxVelocity, pc.maxVelocity);
    }

    // tan(alpha) = |grad b|, so sin(alpha) = |grad| / sqrt(1 + |grad|^2). Central differences,
    // falling back to a one-sided difference against a wall.
    uint cur = pc.terrainParity;
    float hSelf = b[pingPongSlot(idx, cur)];
    float hL = cellValid(left) ? b[pingPongSlot(cellIndex(uint(left.x), uint(left.y)), cur)] : hSelf;
    float hR = cellValid(right) ? b[pingPongSlot(cellIndex(uint(right.x), uint(right.y)), cur)] : hSelf;
    float hT = cellValid(top) ? b[pingPongSlot(cellIndex(uint(top.x), uint(top.y)), cur)] : hSelf;
    float hB = cellValid(bottom) ? b[pingPongSlot(cellIndex(uint(bottom.x), uint(bottom.y)), cur)] : hSelf;

    float gradX = (hR - hL) / (2.0 * pc.cellSize);
    float gradZ = (hB - hT) / (2.0 * pc.cellSize);
    float gradLen = sqrt(gradX * gradX + gradZ * gradZ);
    // The capacity term vanishes on flat ground - the paper calls this out as the model's one blind
    // spot - so the tilt is floored to keep gentle terrain responsive.
    float sinAlpha = max(pc.minTiltSin, gradLen / sqrt(1.0 + gradLen * gradLen));

    d[idx] = d2;
    flow[idx] = vec4(u, w, sinAlpha, 0.0);
}

// ---------------------------------------------------------------------------------------
// Pass 2 - erosion and deposition. Strictly cell-local.
// ---------------------------------------------------------------------------------------
// This pass writes b and s IN PLACE, with no parity flip. That is legal only because the tilt it
// needs was already cached by WATER - without that, the capacity term would read neighbouring
// heights while this pass writes its own. Terrain parity flips only after THERMAL, sediment parity
// only after ADVECT.
void passErosion(uint idx)
{
    uint slot = pingPongSlot(idx, pc.terrainParity);
    uint sSlot = pingPongSlot(idx, pc.sedimentParity);

    float height = b[slot];
    float sed = s[sSlot];
    float depth = d[idx];

    if (depth <= pc.minWater)
    {
        // Dried out: drop what is still in suspension right here. This is what builds the alluvial
        // fan where a channel runs out of water.
        float drop = min(sed, pc.maxStepDelta);
        b[slot] = height + drop;
        s[sSlot] = sed - drop;
        return;
    }

    vec4 v = flow[idx];
    float speed = length(v.xy);
    float capacity = pc.sedimentCapacity * v.z * speed;

    // capacity > sed: dissolve ground into the water. Otherwise: settle sediment out of it.
    // Either way b and s move by equal and opposite amounts, so b + s is conserved.
    float delta = (capacity > sed) ? -pc.dissolveRate * (capacity - sed)
                                   : pc.depositRate * (sed - capacity);
    // Stops one step from cutting deeper than the cell is wide, which the mesh cannot represent and
    // which shows up as a spike.
    delta = clamp(delta, -pc.maxStepDelta, pc.maxStepDelta);

    b[slot] = height + delta;
    s[sSlot] = max(0.0, sed - delta);
}

// ---------------------------------------------------------------------------------------
// Pass 3 - semi-Lagrangian sediment advection, then evaporation
// ---------------------------------------------------------------------------------------
float sampleSediment(vec2 pos)
{
    // Outside the rect reads as zero rather than clamping to the edge: the boundary is absorbing,
    // and clamping would smear rim sediment back inward as a visible band.
    if (pos.x < 0.0 || pos.y < 0.0 ||
        pos.x > float(pc.regionWidth - 1u) || pos.y > float(pc.regionHeight - 1u))
        return 0.0;

    uint x0 = uint(pos.x);
    uint z0 = uint(pos.y);
    uint x1 = min(x0 + 1u, pc.regionWidth - 1u);
    uint z1 = min(z0 + 1u, pc.regionHeight - 1u);
    float fx = pos.x - float(x0);
    float fz = pos.y - float(z0);

    uint parity = pc.sedimentParity;
    float s00 = s[pingPongSlot(cellIndex(x0, z0), parity)];
    float s10 = s[pingPongSlot(cellIndex(x1, z0), parity)];
    float s01 = s[pingPongSlot(cellIndex(x0, z1), parity)];
    float s11 = s[pingPongSlot(cellIndex(x1, z1), parity)];

    return mix(mix(s00, s10, fx), mix(s01, s11, fx), fz);
}

void passAdvect(ivec2 p, uint idx)
{
    vec4 v = flow[idx];
    vec2 src = vec2(float(p.x), float(p.y)) - v.xy * pc.dt / pc.cellSize;

    s[pingPongSlot(idx, 1u - pc.sedimentParity)] = sampleSediment(src);
    d[idx] *= (1.0 - pc.evaporation);
}

// ---------------------------------------------------------------------------------------
// Pass 4 - one talus relaxation sweep, interleaved
// ---------------------------------------------------------------------------------------
//
// The same rule brush_compute.glsl case 6 applies. Interleaved rather than run at the end so the
// inter-channel ridges never get a chance to sharpen: the pipe model carves clean channels but
// leaves spikes between them, which is why Unity's hydraulic tool does the same thing.
void passThermal(ivec2 p, uint idx)
{
    uint src = pingPongSlot(idx, pc.terrainParity);
    uint dst = pingPongSlot(idx, 1u - pc.terrainParity);

    float current = b[src];
    float targetSum = 0.0;
    float violations = 0.0;

    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dz == 0)
                continue;
            ivec2 n = p + ivec2(dx, dz);
            if (!cellValid(n))
                continue;

            float neighbour = b[pingPongSlot(cellIndex(uint(n.x), uint(n.y)), pc.terrainParity)];
            if (current - neighbour > pc.talusThreshold)
            {
                targetSum += neighbour + pc.talusThreshold;
                violations += 1.0;
            }
        }
    }

    float result = current;
    if (violations > 0.0)
    {
        float target = min(current, targetSum / violations);
        result = current + (target - current) * pc.smoothing;
    }
    b[dst] = result;
}

// ---------------------------------------------------------------------------------------
// Pass 5 - settle, blend through the falloff, clamp
// ---------------------------------------------------------------------------------------
//
// The `+ sed` is NOT optional. Erosion moves ground out of the terrain and into suspension;
// whatever is still suspended when the loop ends was removed from the terrain, and if it is not
// returned here it is destroyed. A dab fires every frame the mouse is held, so dropping it drains
// the terrain under the cursor at ~60 dabs a second.
//
// Blending the DELTA is what makes the change exactly zero outside the radius no matter what the
// sim did out in the padded margin: at dist >= 1 the weight is 0 and `base + delta*0 == base`
// bit-exactly.
void passResolve(ivec2 p, uint idx)
{
    float base = bOrig[idx];
    float settled = b[pingPongSlot(idx, pc.terrainParity)] + s[pingPongSlot(idx, pc.sedimentParity)];
    float weight = influenceAt(p) * pc.strengthScale;
    heightOut[idx] = clamp(base + (settled - base) * weight, pc.minHeight, pc.maxHeight);
}

void main()
{
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= pc.regionWidth || gid.y >= pc.regionHeight)
        return;

    ivec2 p = ivec2(gid);
    uint idx = cellIndex(gid.x, gid.y);

    if (validMask[idx] == 0u)
    {
        // A cell no tile owns is never simulated and never scattered back. But the two passes that
        // flip a parity must still carry its value into the new half, or the next iteration reads
        // the stale other half and a wall starts drifting.
        if (pc.passIndex == 5u)
            heightOut[idx] = bOrig[idx];
        else if (pc.passIndex == 4u)
            b[pingPongSlot(idx, 1u - pc.terrainParity)] = b[pingPongSlot(idx, pc.terrainParity)];
        else if (pc.passIndex == 3u)
            s[pingPongSlot(idx, 1u - pc.sedimentParity)] = s[pingPongSlot(idx, pc.sedimentParity)];
        return;
    }

    switch (pc.passIndex)
    {
        case 0u: passFlux(p, idx); break;
        case 1u: passWater(p, idx); break;
        case 2u: passErosion(idx); break;
        case 3u: passAdvect(p, idx); break;
        case 4u: passThermal(p, idx); break;
        case 5u: passResolve(p, idx); break;
    }
}
