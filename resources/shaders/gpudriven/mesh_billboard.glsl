#type MESH
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 32) in;
layout(triangles, max_vertices = 4, max_primitives = 2) out;

struct BillboardInstance {
    vec4 positionAndScale;    // xyz = world position, w = uniform scale
    vec4 atlasUVRect;         // static: xy=UV offset, zw=UV size. animated: xy=scrollU/V
    vec4 colorTint;           // rgba
    uint bindlessTextureIndex;
    uint flags;
    uint entityId;
    float rotation;           // static: radians. animated: spin rate (rad/sec)
    vec2 size;                // width, height in world units
    float flipbookColsRows;   // encoded floor(cols)*256 + rows (animated only)
    float flipbookFrameRate;  // frames/sec (animated only)
};

layout(std430, set = 0, binding = 0) readonly buffer BillboardInstanceBuffer {
    BillboardInstance instances[];
};

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;
    vec4 cameraPosition;     // .w = nearPlane
    vec4 screenParams;
    vec4 frustumPlanes[6];
    float farPlane;
    uint objectCount;
    uint hiZMipLevels;
    uint frameIndex;
    uint enableFrustumCulling;
    uint enableOcclusionCulling;
    uint enableLODSelection;
    uint batchCount;
    uint commandsPerBatch;
    uint shaderGroupCount;
    uint enableDistanceCulling;
    float globalLodBias;
    vec4 categoryDistSq0;
    vec4 categoryDistSq1;
};

struct BillboardPayload {
    uint instanceIndices[32];
};

taskPayloadSharedEXT BillboardPayload payload;

layout(push_constant) uniform Push {
    float uTime; // animation time (seconds)
} pc;

layout(location = 0) out vec2 outUV[];
layout(location = 1) out flat uint outTextureIndex[];
layout(location = 2) out vec4 outColorTint[];

const uint FLAG_AXIS_ALIGNED = 1u;
const uint FLAG_ANIMATED = 2u;

void main() {
    uint tid = gl_LocalInvocationID.x;

    // Each mesh work group renders one billboard instance.
    // The task shader packed visible instance indices into the payload.
    uint instanceIdx = payload.instanceIndices[gl_WorkGroupID.x];
    BillboardInstance inst = instances[instanceIdx];

    SetMeshOutputsEXT(4, 2);

    // Only thread 0 needs to emit the quad (4 vertices, 2 triangles)
    if (tid == 0) {
        vec3 center = inst.positionAndScale.xyz;
        float scale = inst.positionAndScale.w;

        float halfW = inst.size.x * scale * 0.5;
        float halfH = inst.size.y * scale * 0.5;

        vec3 right;
        vec3 up;

        if ((inst.flags & FLAG_AXIS_ALIGNED) != 0u) {
            // Y-axis aligned billboard (cylindrical)
            vec3 toCamera = cameraPosition.xyz - center;
            toCamera.y = 0.0;
            toCamera = normalize(toCamera);
            right = normalize(cross(vec3(0.0, 1.0, 0.0), toCamera));
            up = vec3(0.0, 1.0, 0.0);
        } else {
            // Full camera-facing billboard (spherical)
            vec3 camRight = vec3(view[0][0], view[1][0], view[2][0]);
            vec3 camUp = vec3(view[0][1], view[1][1], view[2][1]);
            right = camRight;
            up = camUp;
        }

        bool animated = (inst.flags & FLAG_ANIMATED) != 0u;

        // Static: rotation is a fixed angle (radians). Animated: rotation is a spin
        // rate (rad/sec) advanced by the push-constant time.
        float effectiveRotation = animated ? (inst.rotation * pc.uTime) : inst.rotation;

        if (effectiveRotation != 0.0) {
            float c = cos(effectiveRotation);
            float s = sin(effectiveRotation);
            vec3 newRight = right * c + up * s;
            vec3 newUp = -right * s + up * c;
            right = newRight;
            up = newUp;
        }

        vec3 positions[4];
        positions[0] = center - right * halfW - up * halfH;
        positions[1] = center + right * halfW - up * halfH;
        positions[2] = center - right * halfW + up * halfH;
        positions[3] = center + right * halfW + up * halfH;

        // Determine the UV sub-rect. Non-animated: use atlasUVRect directly
        // (offset.xy, size.zw). Animated: compute the flipbook frame sub-rect and add
        // a time-based scroll (atlasUVRect.xy = scroll speed, units/sec). This mirrors
        // render::computeFlipbookFrame in FlipbookMath.hpp.
        vec2 uvOff;
        vec2 uvSize;
        if (animated) {
            float colsF = floor(inst.flipbookColsRows / 256.0);
            float rowsF = inst.flipbookColsRows - colsF * 256.0;
            float total = colsF * rowsF;

            if (total <= 1.0 || inst.flipbookFrameRate <= 0.0) {
                uvOff = vec2(0.0, 0.0);
                uvSize = vec2(1.0, 1.0);
            } else {
                float frame = mod(pc.uTime * inst.flipbookFrameRate, total);
                if (frame < 0.0) frame += total;
                float floored = floor(frame);
                float col = mod(floored, colsF);
                float row = floor(floored / colsF);
                uvSize = vec2(1.0 / colsF, 1.0 / rowsF);
                uvOff = vec2(col, row) * uvSize;
            }

            // UV scroll (wraps within the current tile via the sampler's repeat).
            uvOff += inst.atlasUVRect.xy * pc.uTime;
        } else {
            uvOff = inst.atlasUVRect.xy;
            uvSize = inst.atlasUVRect.zw;
        }

        vec2 uvs[4];
        uvs[0] = uvOff + vec2(0.0, uvSize.y);
        uvs[1] = uvOff + vec2(uvSize.x, uvSize.y);
        uvs[2] = uvOff;
        uvs[3] = uvOff + vec2(uvSize.x, 0.0);

        for (uint v = 0; v < 4; v++) {
            gl_MeshVerticesEXT[v].gl_Position = viewProjection * vec4(positions[v], 1.0);
            outUV[v] = uvs[v];
            outTextureIndex[v] = inst.bindlessTextureIndex;
            outColorTint[v] = inst.colorTint;
        }

        gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
        gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
    }
}
