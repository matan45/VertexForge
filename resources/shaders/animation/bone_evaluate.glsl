#type COMPUTE
#version 450

// GPU bone matrix evaluation compute shader
// 1 workgroup per animated entity, 1 thread per bone
// Evaluates keyframe interpolation and hierarchy transforms

layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;

// Per-entity evaluation request
struct AnimEvalRequest
{
    uint animDataIndex;     // Index into animation data array
    uint skeletonIndex;     // Index into skeleton data array
    uint boneCount;
    uint outputBoneOffset;  // Offset into output bone SSBO

    float timeInTicks;
    float blendWeight;      // 0 = no blend, >0 = blend with second anim
    uint secondAnimIndex;
    float secondTime;
};

// Bone hierarchy info (per skeleton)
struct BoneInfo
{
    int parentIndex;
    mat4 preTransform;
    mat4 offsetMatrix;      // Local bind pose
    mat4 inverseBindPose;
};

// Keyframe data (packed for GPU)
struct PositionKey
{
    float time;
    vec3 position;
};

struct RotationKey
{
    float time;
    vec4 rotation;          // quaternion (x, y, z, w)
};

struct ScaleKey
{
    float time;
    vec3 scale;
};

// Channel header (per bone per animation)
struct ChannelHeader
{
    uint positionKeyOffset;
    uint positionKeyCount;
    uint rotationKeyOffset;
    uint rotationKeyCount;
    uint scaleKeyOffset;
    uint scaleKeyCount;
    uint boneIndex;         // Which bone this channel controls
    uint _pad;
};

// Animation clip header
struct AnimClipHeader
{
    float duration;
    float ticksPerSecond;
    uint channelCount;
    uint channelHeaderOffset;
    mat4 globalInverseTransform;
};

// SSBOs
layout(set = 0, binding = 0) readonly buffer EvalRequests
{
    AnimEvalRequest requests[];
};

layout(set = 0, binding = 1) readonly buffer SkeletonBuffer
{
    BoneInfo bones[];  // All skeletons packed sequentially
};

layout(set = 0, binding = 2) readonly buffer AnimClipHeaders
{
    AnimClipHeader clipHeaders[];
};

layout(set = 0, binding = 3) readonly buffer ChannelHeaders
{
    ChannelHeader channelHeaders[];
};

layout(set = 0, binding = 4) readonly buffer PositionKeys
{
    PositionKey positionKeys[];
};

layout(set = 0, binding = 5) readonly buffer RotationKeys
{
    RotationKey rotationKeys[];
};

layout(set = 0, binding = 6) readonly buffer ScaleKeys
{
    ScaleKey scaleKeys[];
};

// Output: bone matrices (same buffer mesh shader reads)
layout(set = 0, binding = 7) writeonly buffer OutputBoneMatrices
{
    mat4 boneMatrices[];
};

// Shared memory for hierarchy traversal
shared mat4 localTransforms[128];
shared mat4 worldTransforms[128];

// Binary search for keyframe index
uint binarySearchPosition(uint offset, uint count, float time)
{
    if (count <= 1) return 0;
    uint lo = 0;
    uint hi = count - 1;
    while (lo < hi)
    {
        uint mid = (lo + hi + 1) / 2;
        if (positionKeys[offset + mid].time <= time)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

uint binarySearchRotation(uint offset, uint count, float time)
{
    if (count <= 1) return 0;
    uint lo = 0;
    uint hi = count - 1;
    while (lo < hi)
    {
        uint mid = (lo + hi + 1) / 2;
        if (rotationKeys[offset + mid].time <= time)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

uint binarySearchScale(uint offset, uint count, float time)
{
    if (count <= 1) return 0;
    uint lo = 0;
    uint hi = count - 1;
    while (lo < hi)
    {
        uint mid = (lo + hi + 1) / 2;
        if (scaleKeys[offset + mid].time <= time)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

vec4 slerp(vec4 q1, vec4 q2, float t)
{
    float cosTheta = dot(q1, q2);
    if (cosTheta < 0.0)
    {
        q2 = -q2;
        cosTheta = -cosTheta;
    }
    if (cosTheta > 0.9995)
    {
        return normalize(mix(q1, q2, t));
    }
    float theta = acos(clamp(cosTheta, -1.0, 1.0));
    float sinTheta = sin(theta);
    float w1 = sin((1.0 - t) * theta) / sinTheta;
    float w2 = sin(t * theta) / sinTheta;
    return w1 * q1 + w2 * q2;
}

mat4 quatToMat4(vec4 q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    return mat4(
        vec4(1.0 - (yy + zz), xy + wz, xz - wy, 0.0),
        vec4(xy - wz, 1.0 - (xx + zz), yz + wx, 0.0),
        vec4(xz + wy, yz - wx, 1.0 - (xx + yy), 0.0),
        vec4(0.0, 0.0, 0.0, 1.0)
    );
}

void main()
{
    uint entityIndex = gl_WorkGroupID.x;
    uint boneIndex = gl_LocalInvocationID.x;

    AnimEvalRequest req = requests[entityIndex];

    if (boneIndex >= req.boneCount)
        return;

    AnimClipHeader clip = clipHeaders[req.animDataIndex];
    float time = req.timeInTicks;
    if (clip.duration > 0.0)
        time = mod(time, clip.duration);

    // Find channel for this bone (linear scan - channels are sparse)
    mat4 animTransform = bones[req.skeletonIndex + boneIndex].offsetMatrix;
    bool foundChannel = false;

    for (uint c = 0; c < clip.channelCount; ++c)
    {
        ChannelHeader ch = channelHeaders[clip.channelHeaderOffset + c];
        if (ch.boneIndex != boneIndex)
            continue;

        foundChannel = true;

        // Interpolate position
        vec3 pos;
        if (ch.positionKeyCount == 0)
        {
            pos = animTransform[3].xyz;
        }
        else if (ch.positionKeyCount == 1)
        {
            pos = positionKeys[ch.positionKeyOffset].position;
        }
        else
        {
            uint idx = binarySearchPosition(ch.positionKeyOffset, ch.positionKeyCount, time);
            uint next = min(idx + 1, ch.positionKeyCount - 1);
            float t0 = positionKeys[ch.positionKeyOffset + idx].time;
            float t1 = positionKeys[ch.positionKeyOffset + next].time;
            float factor = (t1 != t0) ? clamp((time - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
            pos = mix(positionKeys[ch.positionKeyOffset + idx].position,
                      positionKeys[ch.positionKeyOffset + next].position, factor);
        }

        // Interpolate rotation
        vec4 rot;
        if (ch.rotationKeyCount == 0)
        {
            rot = vec4(0.0, 0.0, 0.0, 1.0);
        }
        else if (ch.rotationKeyCount == 1)
        {
            rot = rotationKeys[ch.rotationKeyOffset].rotation;
        }
        else
        {
            uint idx = binarySearchRotation(ch.rotationKeyOffset, ch.rotationKeyCount, time);
            uint next = min(idx + 1, ch.rotationKeyCount - 1);
            float t0 = rotationKeys[ch.rotationKeyOffset + idx].time;
            float t1 = rotationKeys[ch.rotationKeyOffset + next].time;
            float factor = (t1 != t0) ? clamp((time - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
            rot = slerp(rotationKeys[ch.rotationKeyOffset + idx].rotation,
                        rotationKeys[ch.rotationKeyOffset + next].rotation, factor);
        }

        // Interpolate scale
        vec3 scl;
        if (ch.scaleKeyCount == 0)
        {
            scl = vec3(1.0);
        }
        else if (ch.scaleKeyCount == 1)
        {
            scl = scaleKeys[ch.scaleKeyOffset].scale;
        }
        else
        {
            uint idx = binarySearchScale(ch.scaleKeyOffset, ch.scaleKeyCount, time);
            uint next = min(idx + 1, ch.scaleKeyCount - 1);
            float t0 = scaleKeys[ch.scaleKeyOffset + idx].time;
            float t1 = scaleKeys[ch.scaleKeyOffset + next].time;
            float factor = (t1 != t0) ? clamp((time - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
            scl = mix(scaleKeys[ch.scaleKeyOffset + idx].scale,
                      scaleKeys[ch.scaleKeyOffset + next].scale, factor);
        }

        // Build TRS matrix
        mat4 T = mat4(1.0);
        T[3] = vec4(pos, 1.0);
        mat4 R = quatToMat4(rot);
        mat4 S = mat4(1.0);
        S[0][0] = scl.x;
        S[1][1] = scl.y;
        S[2][2] = scl.z;

        animTransform = T * R * S;
        break;
    }

    // Apply pre-transform
    localTransforms[boneIndex] = bones[req.skeletonIndex + boneIndex].preTransform * animTransform;

    // Barrier: all bones must have local transforms computed
    barrier();

    // Phase 2: Compute world transforms top-down
    // Bones are topologically sorted (parent index < child index)
    int parent = bones[req.skeletonIndex + boneIndex].parentIndex;
    if (parent >= 0)
        worldTransforms[boneIndex] = worldTransforms[parent] * localTransforms[boneIndex];
    else
        worldTransforms[boneIndex] = localTransforms[boneIndex];

    // Barrier: world transforms must be computed before final output
    barrier();

    // Phase 3: Apply inverse bind pose and write output
    mat4 result = clip.globalInverseTransform * worldTransforms[boneIndex] *
                  bones[req.skeletonIndex + boneIndex].inverseBindPose;

    boneMatrices[req.outputBoneOffset + boneIndex] = result;
}
