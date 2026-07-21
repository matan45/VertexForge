#ifndef REFLECTION_PROBE_TYPES_GLSL
#define REFLECTION_PROBE_TYPES_GLSL

// VK-1577 — GPU mirror of render::probe::GPUReflectionProbe
// (VFEngine/graphics/render/probe/ReflectionProbeTypes.hpp).
//
// The C++ side static_asserts every offset below. std430 gives mat4 a 64-byte size / 16-byte
// alignment and vec4/uvec4 16 bytes each, so this struct is 224 B and matches the C++ layout
// field for field. Do not reorder — add to `reserved` instead.

// Injected by the pipeline from render::probe::MAX_REFLECTION_PROBES so there is one source of
// truth; the fallback only exists so this file can be validated standalone.
#ifndef MAX_REFLECTION_PROBES
#define MAX_REFLECTION_PROBES 8
#endif

#define PROBE_SHAPE_BOX    0u
#define PROBE_SHAPE_SPHERE 1u

struct GPUReflectionProbe
{
    mat4 worldToLocal;       //   0  world -> unit box [-1,1]
    mat4 localToWorld;       //  64  unit box -> world
    vec4 positionRadius;     // 128  xyz = capture position (world), w = sphere radius
    vec4 blendNormIntensity; // 144  xyz = per-axis normalized blend band, w = intensity
    vec4 boundsMin;          // 160  xyz = world AABB min, w reserved
    vec4 boundsMax;          // 176  xyz = world AABB max, w reserved
    uvec4 params;            // 192  x = cube slot, y = shape, zw reserved
    vec4 reserved;           // 208
};

// Header is { uint count; uint pad[3]; } == 16 B, which is also the std430 alignment the probe
// array needs, so `probes` starts exactly at PROBE_BUFFER_HEADER_SIZE.
layout(std430, set = 0, binding = 5) readonly buffer ReflectionProbeBuffer
{
    uint probeCount;
    uint probePad0;
    uint probePad1;
    uint probePad2;
    GPUReflectionProbe probes[];
} probeData;

// The probe cubes. A descriptor ARRAY of samplerCube, not a samplerCubeArray: the latter needs the
// `imageCubeArray` device feature, which this device is not created with.
//
// Every slot is always written (unallocated ones point at the global prefilter map), so indexing is
// always safe regardless of probeCount.
layout(set = 0, binding = 4) uniform samplerCube probeCubes[MAX_REFLECTION_PROBES];

// Single choke point for the actual texture fetch. Swapping to a real cube array later becomes a
// one-line change here instead of an edit to every consumer.
//
// Requires GL_EXT_nonuniform_qualifier, which both consumers already `require` at the top of the
// file (mesh_shader_gpudriven.glsl, mesh_terrain.glsl). It is not strictly needed while the index
// is the loop counter, but a later "pick the best probe, sample once" refactor would make it
// divergent with no diagnostic — so it is here from the start.
#ifndef PROBE_SAMPLE
#define PROBE_SAMPLE(slot, dir, lod) textureLod(probeCubes[nonuniformEXT(slot)], dir, lod)
#endif

#endif // REFLECTION_PROBE_TYPES_GLSL
