#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require

// GPU-Driven Mesh Shader
// Processes one meshlet per workgroup, fetching vertices from merged buffers
// and outputting triangles to the rasterizer.

// Meshlet limits (must match MeshletBufferTypes.hpp)
const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

// ============================================================================
// Output vertex attributes (to fragment shader)
// ============================================================================
layout(location = 0) out vec3 fragWorldPos[];
layout(location = 1) out vec3 fragNormal[];
layout(location = 2) out vec2 fragTexCoord[];
layout(location = 3) flat out uint fragDrawIndex[];
layout(location = 4) flat out uint fragDebugVertexIndex[];  // Debug: global vertex index
layout(location = 5) flat out uint fragMeshletIndex[];      // Debug: meshlet index for colorization

// ============================================================================
// Per-Draw Data (240 bytes, must match PerDrawData in GPUDrivenTypes.hpp)
// ============================================================================
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;

    vec4 albedo;
    vec4 materialParams;

    uvec4 textureIndices0;
    uvec4 textureIndices1;

    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;

    uint lodLevel;
    uint shaderGroupIndex;
    uint meshletOffset;
    uint meshletCount;

    uint baseVertexOffset;
    uint padding1;
    uint padding2;
    uint padding3;
};

// GPUMeshlet structure (48 bytes, must match GPUMeshlet in MeshletBufferTypes.hpp)
// Layout: vertexOffset(4) + primitiveOffset(4) + [vertexCount(1) + primitiveCount(1) + padding(2)] + globalVertexOffset(4) + boundingSphere(16) + cone(16)
struct GPUMeshlet {
    uint vertexOffset;       // Offset into meshlet vertex index buffer
    uint primitiveOffset;    // Offset into meshlet primitive buffer
    uint vertexPrimCount;    // Packed: (vertexCount | primitiveCount << 8 | padding << 16)
    uint globalVertexOffset; // Base vertex offset in merged vertex buffer

    vec4 boundingSphere;
    vec4 cone;
};

// Vertex data is read as raw floats to avoid std430 vec3 padding issues
// C++ Vertex struct is 32 bytes: position(12) + normal(12) + texCoord(8)
// std430 would pad vec3 to 16 bytes, breaking alignment

// Camera data - MUST match C++ CameraUBO struct in MeshTypes.hpp
// The IBL descriptor set uses this smaller struct, not GPUCameraData
struct CameraData {
    mat4 view;           // 64 bytes
    mat4 projection;     // 64 bytes
    vec3 cameraPos;      // 12 bytes (aligned to 16)
    float time;          // 4 bytes
    // Total: 144 bytes
};

// ============================================================================
// Descriptor Bindings
// ============================================================================

// Set 0: Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Set 3: Meshlet data
layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 3, binding = 1) readonly buffer MeshletVertexBuffer {
    uint meshletVertices[];  // Local vertex indices into merged buffer
};

layout(std430, set = 3, binding = 2) readonly buffer MeshletPrimitiveBuffer {
    uint meshletPrimitives[];  // Packed triangle indices (3 uint8 per triangle)
};

// Set 4: Merged vertex data as raw floats (32 bytes per vertex)
// Layout per vertex: position.xyz (12), normal.xyz (12), texCoord.xy (8) = 32 bytes = 8 floats
layout(std430, set = 4, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];  // Raw float array to avoid std430 vec3 padding
};

// ============================================================================
// Task Payload (received from task shader)
// ============================================================================
const uint MAX_MESHLETS_PER_PAYLOAD = 32;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT MeshletPayload payload;

// Push constants (shared with task/fragment shaders)
layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;  // Used by task shader
    uint padding;
    float screenWidth;   // Used by fragment shader
    float screenHeight;  // Used by fragment shader
} pc;

// Shared memory for vertex/primitive data
shared vec3 sharedPositions[MESHLET_MAX_VERTICES];
shared vec3 sharedNormals[MESHLET_MAX_VERTICES];
shared vec2 sharedTexCoords[MESHLET_MAX_VERTICES];

// ============================================================================
// Helper Functions
// ============================================================================

// Unpack primitive indices from packed uint32
// Format: [idx0 | idx1 | idx2 | padding] in little-endian
uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

// Unpack vertex count and primitive count from packed uint32
// Format: [vertexCount(8 bits) | primitiveCount(8 bits) | padding(16 bits)]
void unpackMeshletCounts(uint packed, out uint vertexCount, out uint primitiveCount) {
    vertexCount = packed & 0xFFu;
    primitiveCount = (packed >> 8) & 0xFFu;
}

// Debug mode: set to 1 to output a hardcoded triangle (bypasses all data loading)
const int DEBUG_HARDCODED_TRIANGLE = 0;

// Debug mode: set to 1 to output simple triangles at meshlet bounding sphere centers
const int DEBUG_MESHLET_BOUNDS = 0;

// ============================================================================
// Main
// ============================================================================
void main() {
    // DEBUG: Output a hardcoded triangle to verify the shader pipeline works
    if (DEBUG_HARDCODED_TRIANGLE != 0) {
        // ALL workgroups output the test triangle (not just first one)
        SetMeshOutputsEXT(3, 1);

        if (gl_LocalInvocationID.x == 0) {
            // Offset based on workgroup ID so triangles don't overlap
            float offsetX = float(gl_WorkGroupID.x) * 0.1;

            // Hardcoded triangle in clip space - try both winding orders
            // Vulkan NDC: Y points down, so flip Y for correct orientation
            gl_MeshVerticesEXT[0].gl_Position = vec4(-0.5 + offsetX,  0.5, 0.5, 1.0);  // top-left
            gl_MeshVerticesEXT[1].gl_Position = vec4( 0.5 + offsetX,  0.5, 0.5, 1.0);  // top-right
            gl_MeshVerticesEXT[2].gl_Position = vec4( 0.0 + offsetX, -0.5, 0.5, 1.0);  // bottom-center

            fragWorldPos[0] = vec3(0.0);
            fragWorldPos[1] = vec3(1.0, 0.0, 0.0);
            fragWorldPos[2] = vec3(0.0, 1.0, 0.0);

            fragNormal[0] = vec3(0.0, 0.0, 1.0);
            fragNormal[1] = vec3(0.0, 0.0, 1.0);
            fragNormal[2] = vec3(0.0, 0.0, 1.0);

            fragTexCoord[0] = vec2(0.0, 0.0);
            fragTexCoord[1] = vec2(1.0, 0.0);
            fragTexCoord[2] = vec2(0.5, 1.0);

            // Use index 0 - this should have valid perDrawData
            // Or use a special marker (0xFFFFFFFF) that fragment shader can detect
            fragDrawIndex[0] = 0xFFFFFFFEu;  // Special marker for debug mode
            fragDrawIndex[1] = 0xFFFFFFFEu;
            fragDrawIndex[2] = 0xFFFFFFFEu;

            fragDebugVertexIndex[0] = 0;
            fragDebugVertexIndex[1] = 1;
            fragDebugVertexIndex[2] = 2;

            gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
        }
        return;
    }

    // Each mesh shader workgroup processes one meshlet
    // gl_WorkGroupID.x indexes into the visible meshlet list in payload
    uint payloadMeshletIndex = gl_WorkGroupID.x;

    // DEBUG: Output simple triangles - NO buffer reads except camera
    // This must be BEFORE the payload.meshletCount check!
    if (DEBUG_MESHLET_BOUNDS != 0) {
        // Output a small triangle at world origin using only camera matrix
        SetMeshOutputsEXT(3, 1);

        if (gl_LocalInvocationID.x == 0) {
            // Compute viewProjection manually since IBL CameraUBO doesn't have it precomputed
            mat4 viewProj = camera.projection * camera.view;

            // Hardcoded world position - triangle at origin
            float size = 0.5;
            float offset = float(gl_WorkGroupID.x) * 0.2;  // Offset per workgroup
            vec3 worldPos = vec3(offset, 0.0, 0.0);

            vec4 p0 = viewProj * vec4(worldPos + vec3(-size, -size, 0.0), 1.0);
            vec4 p1 = viewProj * vec4(worldPos + vec3( size, -size, 0.0), 1.0);
            vec4 p2 = viewProj * vec4(worldPos + vec3( 0.0,  size, 0.0), 1.0);

            gl_MeshVerticesEXT[0].gl_Position = p0;
            gl_MeshVerticesEXT[1].gl_Position = p1;
            gl_MeshVerticesEXT[2].gl_Position = p2;

            fragWorldPos[0] = worldPos;
            fragWorldPos[1] = worldPos;
            fragWorldPos[2] = worldPos;

            fragNormal[0] = vec3(0.0, 0.0, 1.0);
            fragNormal[1] = vec3(0.0, 0.0, 1.0);
            fragNormal[2] = vec3(0.0, 0.0, 1.0);

            fragTexCoord[0] = vec2(0.0, 0.0);
            fragTexCoord[1] = vec2(1.0, 0.0);
            fragTexCoord[2] = vec2(0.5, 1.0);

            fragDrawIndex[0] = 0xFFFFFFFEu;  // Debug marker
            fragDrawIndex[1] = 0xFFFFFFFEu;
            fragDrawIndex[2] = 0xFFFFFFFEu;

            fragDebugVertexIndex[0] = gl_WorkGroupID.x;
            fragDebugVertexIndex[1] = gl_WorkGroupID.x;
            fragDebugVertexIndex[2] = gl_WorkGroupID.x;

            gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
        }
        return;
    }

    // Bounds check - should not happen if task shader is correct
    if (payloadMeshletIndex >= payload.meshletCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    // Get the global meshlet index from payload
    uint globalMeshletIndex = payload.meshletIndices[payloadMeshletIndex];
    uint drawIndex = payload.drawIndex;

    // Fetch meshlet descriptor
    GPUMeshlet meshlet = meshlets[globalMeshletIndex];

    // Fetch per-draw data
    PerDrawData drawData = perDrawData[drawIndex];

    // Unpack vertex and primitive counts
    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);

    // Set mesh outputs
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    // Get matrices for transformations
    mat4 modelMatrix = drawData.modelMatrix;
    mat3 normalMatrix = mat3(drawData.normalMatrix);
    mat4 viewProjection = camera.projection * camera.view;

    // Phase 1: Load vertices into shared memory (collaborative loading)
    uint numIterations = (vertexCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;

    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;

        if (localVertexIndex < vertexCount) {
            // Get the vertex index from meshlet's local index buffer
            uint meshletLocalVertexIdx = meshletVertices[meshlet.vertexOffset + localVertexIndex];

            // Calculate global vertex index in merged buffer
            uint globalVertexIndex = meshlet.globalVertexOffset + meshletLocalVertexIdx;

            // Load vertex data from raw float array (8 floats per vertex = 32 bytes)
            // Layout: position.xyz (0-2), normal.xyz (3-5), texCoord.xy (6-7)
            uint baseIdx = globalVertexIndex * 8;
            sharedPositions[localVertexIndex] = vec3(
                vertexData[baseIdx + 0],
                vertexData[baseIdx + 1],
                vertexData[baseIdx + 2]
            );
            sharedNormals[localVertexIndex] = vec3(
                vertexData[baseIdx + 3],
                vertexData[baseIdx + 4],
                vertexData[baseIdx + 5]
            );
            sharedTexCoords[localVertexIndex] = vec2(
                vertexData[baseIdx + 6],
                vertexData[baseIdx + 7]
            );
        }
    }

    barrier();

    // Phase 2: Output vertices (collaborative)
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;

        if (localVertexIndex < vertexCount) {
            // Transform position to world space
            vec4 worldPos = modelMatrix * vec4(sharedPositions[localVertexIndex], 1.0);
            fragWorldPos[localVertexIndex] = worldPos.xyz;

            // Transform normal to world space
            fragNormal[localVertexIndex] = normalize(normalMatrix * sharedNormals[localVertexIndex]);

            // Pass through texture coordinates
            fragTexCoord[localVertexIndex] = sharedTexCoords[localVertexIndex];

            // Pass draw index for fragment shader
            fragDrawIndex[localVertexIndex] = drawIndex;

            // Debug: pass the global vertex index
            uint meshletLocalVertexIdx = meshletVertices[meshlet.vertexOffset + localVertexIndex];
            fragDebugVertexIndex[localVertexIndex] = meshlet.globalVertexOffset + meshletLocalVertexIdx;

            // Debug: pass the meshlet index for colorization
            fragMeshletIndex[localVertexIndex] = globalMeshletIndex;

            // Output clip-space position
            gl_MeshVerticesEXT[localVertexIndex].gl_Position = viewProjection * worldPos;
        }
    }

    // Phase 3: Output primitives (collaborative)
    uint numPrimIterations = (primitiveCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;

    for (uint iter = 0; iter < numPrimIterations; iter++) {
        uint localPrimIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;

        if (localPrimIndex < primitiveCount) {
            // Fetch packed primitive data
            uint packedPrimitive = meshletPrimitives[meshlet.primitiveOffset + localPrimIndex];

            // Unpack triangle vertex indices (local to meshlet)
            uvec3 indices = unpackPrimitive(packedPrimitive);

            // Output primitive
            gl_PrimitiveTriangleIndicesEXT[localPrimIndex] = indices;
        }
    }
}

#type FRAGMENT
#version 460 core

// GPU-Driven Mesh Shader Fragment
// Uses bindless textures and per-draw data from storage buffer
// This is the same PBR fragment shader used for traditional rendering

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragDrawIndex;
layout(location = 4) in flat uint fragDebugVertexIndex;
layout(location = 5) in flat uint fragMeshletIndex;

layout(location = 0) out vec4 outColor;

// Camera data - MUST match C++ CameraUBO struct in MeshTypes.hpp
// The IBL descriptor set uses this smaller struct, not GPUCameraData
struct CameraData {
    mat4 view;           // 64 bytes
    mat4 projection;     // 64 bytes
    vec3 cameraPos;      // 12 bytes (aligned to 16)
    float time;          // 4 bytes
    // Total: 144 bytes
};

// Set 0: Camera and IBL resources
layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Per-draw data structure (must match mesh shader and GPUDrivenTypes.hpp)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;

    vec4 albedo;
    vec4 materialParams;

    uvec4 textureIndices0;
    uvec4 textureIndices1;

    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;

    uint lodLevel;
    uint shaderGroupIndex;
    uint meshletOffset;
    uint meshletCount;

    uint baseVertexOffset;
    uint padding1;
    uint padding2;
    uint padding3;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Set 2: Bindless texture array
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

// Push constants (shared with task/mesh shaders)
layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;    // Used by task shader
    uint viewMode;         // 0=Color, 1=Meshlet, 2=LOD visualization mode
    float screenWidth;     // Screen width in pixels
    float screenHeight;    // Screen height in pixels
} pc;

// Constants
const float PI = 3.14159265359;
const float ALPHA_CUTOFF = 0.5;
const float MAX_REFLECTION_LOD = 4.0;
const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;

// Object flags (must match ObjectFlags namespace in GPUDrivenTypes.hpp)
const uint FLAG_ALPHA_MASK = 1u << 4;

// View modes (controlled via push constant pc.viewMode):
// 0 = Color (normal PBR rendering)
// 1 = Meshlet (visualize each meshlet with unique color)
// 2 = LOD (color-coded by LOD level)

// Helper: Check if texture index is valid
bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

// Unpack ORM texture: R=AO, G=Roughness, B=Metallic
vec3 unpackORM(vec4 ormSample) {
    return vec3(ormSample.r, ormSample.g, ormSample.b);
}

// PBR Functions
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // DEBUG: Check for debug marker from mesh shader
    if (fragDrawIndex == 0xFFFFFFFEu) {
        // Output solid magenta color for debug triangle
        outColor = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    PerDrawData drawData = perDrawData[fragDrawIndex];

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Get texture indices
    uint albedoIdx = drawData.textureIndices0.x;
    uint normalIdx = drawData.textureIndices0.y;
    uint ormIdx = drawData.textureIndices0.z;
    uint metallicIdx = drawData.textureIndices0.w;
    uint roughnessIdx = drawData.textureIndices1.x;
    uint aoIdx = drawData.textureIndices1.y;
    uint emissionIdx = drawData.textureIndices1.z;

    vec2 texCoords = fragTexCoord;

    // Apply time-based UV animation for materials with Time node (shaderGroupIndex > 0)
    if (drawData.shaderGroupIndex > 0u) {
        texCoords += vec2(camera.time * 0.1, 0.0);
    }

    // Calculate texture coordinate derivatives for proper mipmap selection
    vec2 texDx = dFdx(fragTexCoord);
    vec2 texDy = dFdy(fragTexCoord);

    // Sample albedo
    vec3 albedo = drawData.albedo.rgb;
    float alpha = drawData.albedo.a;

    if (isValidTexture(albedoIdx)) {
        vec4 albedoSample = textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], texCoords, texDx, texDy);
        albedo = pow(albedoSample.rgb, vec3(2.2));
        alpha = albedoSample.a;
    }

    // Alpha masking
    if ((drawData.flags & FLAG_ALPHA_MASK) != 0u) {
        if (alpha < ALPHA_CUTOFF) {
            discard;
        }
    }

    // Sample PBR values
    float metallic = drawData.materialParams.x;
    float roughness = drawData.materialParams.y;
    float ao = drawData.materialParams.z;
    float emission = drawData.materialParams.w;

    if (isValidTexture(ormIdx)) {
        vec3 ormValues = unpackORM(textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], texCoords, texDx, texDy));
        ao = ormValues.x;
        roughness = ormValues.y;
        metallic = ormValues.z;
    } else {
        if (isValidTexture(metallicIdx)) {
            metallic = textureGrad(bindlessTextures[nonuniformEXT(metallicIdx)], texCoords, texDx, texDy).r;
        }
        if (isValidTexture(roughnessIdx)) {
            roughness = textureGrad(bindlessTextures[nonuniformEXT(roughnessIdx)], texCoords, texDx, texDy).r;
        }
        if (isValidTexture(aoIdx)) {
            ao = textureGrad(bindlessTextures[nonuniformEXT(aoIdx)], texCoords, texDx, texDy).r;
        }
    }

    // Normal mapping
    if (isValidTexture(normalIdx)) {
        // Construct TBN matrix from screen-space derivatives
        vec3 pos_dx = dFdx(fragWorldPos);
        vec3 pos_dy = dFdy(fragWorldPos);
        vec2 uv_dx = dFdx(fragTexCoord);
        vec2 uv_dy = dFdy(fragTexCoord);

        vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
        vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);
        mat3 TBN = mat3(T, B, N);

        vec3 tangentNormal = textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], texCoords, texDx, texDy).rgb * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

    vec3 R = reflect(-V, N);

    // Calculate F0 (reflectance at normal incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // IBL Ambient Lighting
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    // Diffuse IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo * drawData.iblDiffuse;

    // Specular IBL
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y) * drawData.iblSpecular;

    // Combine ambient
    vec3 ambient = (kD * diffuse + specular) * ao;

    // Add emission
    vec3 emissive = vec3(0.0);
    if (isValidTexture(emissionIdx)) {
        emissive = pow(textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], texCoords, texDx, texDy).rgb, vec3(2.2)) * emission;
    } else {
        emissive = albedo * emission;
    }

    vec3 color = ambient + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    // Meshlet view mode (pc.viewMode == 1): visualize each meshlet with a unique color
    if (pc.viewMode == 1u) {
        // Generate a unique color per meshlet using hash-based approach
        uint h = fragMeshletIndex;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = (h >> 16) ^ h;

        vec3 meshletColor = vec3(
            float((h >> 0) & 0xFFu) / 255.0,
            float((h >> 8) & 0xFFu) / 255.0,
            float((h >> 16) & 0xFFu) / 255.0
        );
        // Boost saturation for more vivid colors
        meshletColor = normalize(meshletColor + 0.1) * 0.8;
        color = meshletColor;
    }

    // LOD view mode (pc.viewMode == 2): visualize LOD levels with colors
    if (pc.viewMode == 2u) {
        vec3 lodColors[4] = vec3[4](
            vec3(0.0, 1.0, 0.0),   // LOD0: Green
            vec3(1.0, 1.0, 0.0),   // LOD1: Yellow
            vec3(1.0, 0.5, 0.0),   // LOD2: Orange
            vec3(1.0, 0.0, 0.0)    // LOD3: Red
        );
        uint lod = min(drawData.lodLevel, 3u);
        color = mix(color, lodColors[lod], 0.5);
    }

    outColor = vec4(color, alpha);
}
