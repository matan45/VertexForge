#type VERTEX
#version 460 core

// Full-screen triangle (no vertex buffer needed)
layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;
layout(set = 0, binding = 2) uniform SSGIParams {
    mat4 projection;
    mat4 inverseProjection;
    mat4 viewMatrix;
    vec4 params;        // x=intensity, y=radius, z=thickness, w=rayCount
    vec4 screenParams;  // x=width, y=height, z=1/width, w=1/height
    float nearPlane;
    float farPlane;
    int stepCount;
    float frameRandom;
};

const float PI = 3.14159265359;

float linearizeDepth(float depth) {
    return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}

vec3 getViewPos(vec2 uv) {
    float depth = texture(sceneDepth, uv).r;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = inverseProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Hash function for blue-noise-like distribution
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 vogelDisk(int sampleIndex, int sampleCount, float phi) {
    float goldenAngle = 2.399963; // ~137.5 degrees
    float r = sqrt(float(sampleIndex) + 0.5) / sqrt(float(sampleCount));
    float theta = float(sampleIndex) * goldenAngle + phi;
    return vec2(cos(theta), sin(theta)) * r;
}

void main() {
    float intensity = params.x;
    float radius = params.y;
    float thickness = params.z;
    int rayCount = int(params.w);

    vec3 viewPos = getViewPos(fragTexCoord);

    // Skip sky pixels
    float depth = texture(sceneDepth, fragTexCoord).r;
    if (depth >= 1.0) {
        outColor = vec4(0.0);
        return;
    }

    // Reconstruct view-space normal from depth
    vec3 viewPosRight = getViewPos(fragTexCoord + vec2(screenParams.z, 0.0));
    vec3 viewPosUp = getViewPos(fragTexCoord + vec2(0.0, screenParams.w));
    vec3 N = normalize(cross(viewPosRight - viewPos, viewPosUp - viewPos));

    float randomRotation = hash(fragTexCoord * screenParams.xy + frameRandom) * 2.0 * PI;

    vec3 indirectLight = vec3(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < rayCount; ++i) {
        // Generate ray direction on hemisphere oriented along normal
        vec2 disk = vogelDisk(i, rayCount, randomRotation);
        vec3 rayDir = normalize(vec3(disk.x, disk.y, 0.5));

        // Orient to normal hemisphere using TBN
        vec3 T = normalize(cross(N, abs(N.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
        vec3 B = cross(N, T);
        rayDir = normalize(T * rayDir.x + B * rayDir.y + N * rayDir.z);

        // Screen-space ray march
        vec3 rayOrigin = viewPos;
        vec3 rayEnd = rayOrigin + rayDir * radius;

        // Project start and end to screen space
        vec4 startClip = projection * vec4(rayOrigin, 1.0);
        vec4 endClip = projection * vec4(rayEnd, 1.0);
        vec2 startScreen = (startClip.xy / startClip.w) * 0.5 + 0.5;
        vec2 endScreen = (endClip.xy / endClip.w) * 0.5 + 0.5;

        vec2 rayStep = (endScreen - startScreen) / float(stepCount);
        vec2 sampleUV = startScreen;

        bool hitFound = false;
        vec3 hitColor = vec3(0.0);

        for (int step = 1; step <= stepCount; ++step) {
            sampleUV += rayStep;

            // Bounds check
            if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0)))) {
                break;
            }

            float sampleDepth = texture(sceneDepth, sampleUV).r;
            vec3 sampleViewPos = getViewPos(sampleUV);

            // Interpolated ray depth at this step
            float t = float(step) / float(stepCount);
            vec3 expectedPos = mix(rayOrigin, rayEnd, t);

            float depthDiff = expectedPos.z - sampleViewPos.z;

            // Hit test: behind the surface but within thickness
            if (depthDiff > 0.0 && depthDiff < thickness) {
                hitColor = texture(sceneColor, sampleUV).rgb;
                hitFound = true;
                break;
            }
        }

        if (hitFound) {
            float NdotR = max(dot(N, rayDir), 0.0);
            float weight = NdotR;
            indirectLight += hitColor * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0) {
        indirectLight /= totalWeight;
    }

    outColor = vec4(indirectLight * intensity, 1.0);
}
