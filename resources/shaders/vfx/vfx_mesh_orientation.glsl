// VK-1476 — shared mesh-particle orientation math.
//
// Included by BOTH vfx_mesh_particle.glsl (GPU runtime) and vfx_mesh_preview.glsl
// (editor CPU-fed preview) so the preview matches runtime orientation BY
// CONSTRUCTION. This file is the SOURCE OF TRUTH; keep VFEngine/utilities/vfx/
// VFXOrientationMath.hpp (the doctest spec) in lockstep with it.
//
// Every function returns a mat3 whose columns are the mesh-local X,Y,Z axes in
// world space — a proper rotation (det +1) so back-face winding is preserved:
//   worldPos = particlePos + basis * (meshVertex * size).
// Dependency-free (primitives only) so it can be included without pulling the sim.

// Orientation mode values (mirror vfx::VFXOrientationMode).
const uint VFX_ORIENT_VELOCITY_FORWARD = 0u;
const uint VFX_ORIENT_TUMBLE = 1u;
const uint VFX_ORIENT_AXIS_LOCK = 2u;
const uint VFX_ORIENT_CAMERA_FACING = 3u;

// --- RNG primitives (bit-exact mirror of vfx_particle_sim.glsl) --------------

uint vfxoHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float vfxoRandomFloat(inout uint seed)
{
    seed = vfxoHash(seed);
    return float(seed) / float(0xFFFFFFFFu);
}

// Uniformly-distributed unit vector on the sphere, deterministic from `seed`.
vec3 vfxoRandomUnitVector(uint seed)
{
    uint s = seed ^ 0x9E3779B9u;
    float theta = vfxoRandomFloat(s) * 6.28318530718;
    float u = vfxoRandomFloat(s) * 2.0 - 1.0;
    u = clamp(u, -1.0, 1.0);
    float phi = acos(u);
    float sinPhi = sin(phi);
    return vec3(sinPhi * cos(theta), cos(phi), sinPhi * sin(theta));
}

// Per-particle spin-rate multiplier in [0.5, 1.5], deterministic from `seed`.
float vfxoRateJitter(uint seed)
{
    uint s = seed ^ 0x85EBCA6Bu;
    return 0.5 + vfxoRandomFloat(s);
}

// Rotate v by quaternion q = (xyz = axis*sin(a/2), w = cos(a/2)).
vec3 vfxoRotateByQuat(vec3 v, vec4 q)
{
    vec3 u = q.xyz;
    float s = q.w;
    return 2.0 * dot(u, v) * u + (s * s - dot(u, u)) * v + 2.0 * s * cross(u, v);
}

// --- Per-mode bases ----------------------------------------------------------

mat3 vfxoAxisAngleBasis(vec3 unitAxis, float angle)
{
    float halfAngle = angle * 0.5;
    float sh = sin(halfAngle);
    vec4 q = vec4(unitAxis * sh, cos(halfAngle));
    return mat3(vfxoRotateByQuat(vec3(1.0, 0.0, 0.0), q),
                vfxoRotateByQuat(vec3(0.0, 1.0, 0.0), q),
                vfxoRotateByQuat(vec3(0.0, 0.0, 1.0), q));
}

// Mode 0. Verbatim port of the legacy vfx_mesh_particle.glsl basis: mesh-local +Z
// follows velocity, roll about it by `rotation`. Byte-identical default.
mat3 vfxVelocityForwardBasis(vec3 velocity, float rotation)
{
    vec3 forward = vec3(0.0, 1.0, 0.0);
    float speed = length(velocity);
    if (speed > 0.001) {
        forward = velocity / speed;
    }

    vec3 up = abs(forward.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, forward));
    up = cross(forward, right);

    float cosR = cos(rotation);
    float sinR = sin(rotation);
    vec3 rotRight = right * cosR + up * sinR;
    vec3 rotUp = -right * sinR + up * cosR;
    return mat3(rotRight, rotUp, forward);
}

// Mode 2. Spin about a fixed axis by `angle`, with a zero-axis fallback to (0,1,0).
mat3 vfxAxisLockBasis(vec3 axis, float angle)
{
    float len = length(axis);
    vec3 unitAxis = (len < 1e-6) ? vec3(0.0, 1.0, 0.0) : axis / len;
    return vfxoAxisAngleBasis(unitAxis, angle);
}

// Mode 1. Per-particle random axis (from spawnSeed) spun by `angle`.
mat3 vfxTumbleBasis(uint spawnSeed, float angle)
{
    return vfxoAxisAngleBasis(vfxoRandomUnitVector(spawnSeed), angle);
}

// Mode 3. Align to the camera basis (columns: right, up, toward-camera) and roll.
mat3 vfxCameraFacingBasis(mat3 camBasis, float rotation)
{
    vec3 camRight = camBasis[0];
    vec3 camUp = camBasis[1];
    vec3 camForward = camBasis[2];
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    vec3 rotRight = camRight * cosR + camUp * sinR;
    vec3 rotUp = -camRight * sinR + camUp * cosR;
    return mat3(rotRight, rotUp, camForward);
}

// Dispatcher — single entry point.
//   params.xyz = axis-lock axis (world), params.w = spin rate (rad/s)
//   age        = seconds since spawn (particle lifetime, counts up)
// Tumble/AxisLock spin by rate*age (Tumble jitters the rate per particle);
// VelocityForward/CameraFacing roll by `rotation`.
mat3 vfxComputeMeshOrientation(uint mode, vec3 velocity, float rotation,
                               uint spawnSeed, float age, vec4 params, mat3 camBasis)
{
    if (mode == VFX_ORIENT_TUMBLE) {
        float rate = params.w * vfxoRateJitter(spawnSeed);
        return vfxTumbleBasis(spawnSeed, rate * age);
    } else if (mode == VFX_ORIENT_AXIS_LOCK) {
        float rate = params.w;
        return vfxAxisLockBasis(params.xyz, rate * age);
    } else if (mode == VFX_ORIENT_CAMERA_FACING) {
        return vfxCameraFacingBasis(camBasis, rotation);
    }
    return vfxVelocityForwardBasis(velocity, rotation);
}
