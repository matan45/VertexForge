// Shared atmosphere UBO definition — must match C++ AtmosphereGPUParams exactly
// All vec4/mat4 to guarantee std140 alignment

vec4 planetParams;        // x=planetRadius, y=atmosphereRadius
vec4 rayleighScattering;  // xyz=scattering, w=densityExpScale
vec4 mieParams;           // x=scattering, y=absorption, z=anisotropy, w=densityExpScale
vec4 ozoneAbsorption;     // xyz=absorption, w=centerAlt
vec4 ozoneParams;         // x=ozoneWidth
vec4 sunIrradiance;       // xyz=irradiance, w=angularRadius
vec4 sunDirection;        // xyz=direction
vec4 groundAlbedo;        // xyz=albedo
vec4 cameraPosition;      // xyz=world pos, w=altitude
mat4 invViewProjection;
mat4 viewProjection;
vec4 screenParams;        // x=nearPlane, y=farPlane, z=aerialMaxDist, w=aerialIntensity
uvec4 screenSize;         // x=width, y=height
