#type COMPUTE
#version 460 core

// VK-1606: one step of the interactive ripple patch.
//
// GPU twin of VFEngine/utilities/water/RippleSimMath.hpp. There is no codegen between them - the
// unit tests exercise the C++ side (analytic damped-oscillator agreement, the CFL bound, the impulse
// kernel, scroll re-indexing) and this shader has to stay in step with it or the tests stop meaning
// anything.
//
// Model: damped 2-D wave equation, symplectic Euler, 5-point Laplacian. Deliberately NOT the
// shallow-water equations - see the header comment in RippleSimMath.hpp.
//
// Update, injection and output composition all happen in ONE pass. The state ping-pong is internal
// (never bound to the water pipeline); only `rippleOutput` is, which is what lets set 9 binding 4 be
// written once at init and never touched again.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) readonly  uniform image2D prevState;   // h, v, foam, -
layout(rgba16f, set = 0, binding = 1) writeonly uniform image2D currState;
layout(rgba16f, set = 0, binding = 2) writeonly uniform image2D rippleOutput; // h, nx, nz, foam

struct WaterImpulse {
    vec2 positionXZ;
    float radius;
    float strength;
};
layout(std430, set = 0, binding = 3) readonly buffer ImpulseBuffer {
    WaterImpulse impulses[];
};

layout(push_constant) uniform PushConstants {
    vec2 originXZ;          //  0  texel-snapped min corner of the patch, world XZ
    vec2 prevOriginXZ;      //  8  last step's origin - the difference is a whole number of texels
    float patchSize;        // 16  metres
    float dt;               // 20  fixed simulation step
    float waveSpeed;        // 24  already clamped to the CFL bound on the CPU
    float damping;          // 28  per-step velocity multiplier
    uint  impulseCount;     // 32
    float foamGain;         // 36
    float foamDecay;        // 40  per-step foam multiplier
    uint  resolution;       // 44
} pc;                       // 48

// Twin of water::rippleImpulseKernel. Compact support with zero first and second derivative at the
// rim - a kink there would inject high-frequency ringing the damping term cannot remove.
float rippleKernel(float dist, float radius)
{
    if (radius <= 0.0) return 0.0;
    float t = clamp(dist / radius, 0.0, 1.0);
    float s = 1.0 - t * t;
    return s * s * s;
}

// Reads the PREVIOUS window at the texel holding the same patch of water as `idx` does now. Both
// origins are snapped to the same lattice, so `scrollOffset` is an exact integer shift rather than a
// resample - which is the whole reason the origin is snapped at all.
vec4 loadPrev(ivec2 idx, ivec2 scrollOffset)
{
    ivec2 s = idx + scrollOffset;
    // Newly scrolled-in water has no history. Read ZERO, not a clamped border texel, or the edge row
    // would be smeared across everything the camera reveals.
    if (s.x < 0 || s.y < 0 || s.x >= int(pc.resolution) || s.y >= int(pc.resolution))
        return vec4(0.0);
    return imageLoad(prevState, s);
}

void main()
{
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (id.x >= int(pc.resolution) || id.y >= int(pc.resolution))
        return;

    float texelSize = pc.patchSize / float(pc.resolution);
    ivec2 scrollOffset = ivec2(round((pc.originXZ - pc.prevOriginXZ) / texelSize));

    vec4 c = loadPrev(id, scrollOffset);
    float hL = loadPrev(id + ivec2(-1, 0), scrollOffset).x;
    float hR = loadPrev(id + ivec2( 1, 0), scrollOffset).x;
    float hD = loadPrev(id + ivec2( 0, -1), scrollOffset).x;
    float hU = loadPrev(id + ivec2( 0, 1), scrollOffset).x;

    float lap = (hL + hR + hD + hU - 4.0 * c.x) / (texelSize * texelSize);

    // Velocity first, then height with the NEW velocity: that ordering is what makes the scheme
    // symplectic, and therefore stable all the way up to c*dt/dx = 1/sqrt(2).
    float vel = (c.y + pc.waveSpeed * pc.waveSpeed * lap * pc.dt) * pc.damping;

    // Impulses inject VELOCITY. A height injection would drop a bump that immediately splits into
    // two counter-propagating rings and reads as a double hit.
    vec2 worldXZ = pc.originXZ + (vec2(id) + 0.5) * texelSize;
    float impulseFoam = 0.0;
    for (uint i = 0u; i < pc.impulseCount; ++i)
    {
        float k = rippleKernel(distance(worldXZ, impulses[i].positionXZ), impulses[i].radius);
        if (k <= 0.0)
            continue;
        vel += impulses[i].strength * k;
        impulseFoam += abs(impulses[i].strength) * k;
    }

    float height = c.x + vel * pc.dt;

    // Curvature is what a real surface foams on. max() rather than += so foam decays from its own
    // history but is instantly re-established by a new disturbance.
    float foam = max(c.z * pc.foamDecay,
                     clamp(abs(lap) * pc.foamGain, 0.0, 1.0) + impulseFoam);
    foam = clamp(foam, 0.0, 1.0);

    imageStore(currState, id, vec4(height, vel, foam, 0.0));

    // The normal comes from the four neighbour heights already fetched for the Laplacian, so it
    // costs nothing extra - at the price of describing the field one step before the height written
    // alongside it. At 60 Hz and a few m/s that is a fraction of a texel and invisible; a second
    // dispatch to remove it would not pay for itself.
    //
    // For a height field y = h(x, z) the surface normal is normalize(-dh/dx, 1, -dh/dz); the water
    // vertex shader adds (nx, 0, nz) to the FFT normal, so store the gradient offset directly.
    float dhdx = (hR - hL) / (2.0 * texelSize);
    float dhdz = (hU - hD) / (2.0 * texelSize);

    imageStore(rippleOutput, id, vec4(height, -dhdx, -dhdz, foam));
}
