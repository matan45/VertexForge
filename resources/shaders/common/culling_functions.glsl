#ifndef CULLING_FUNCTIONS_GLSL
#define CULLING_FUNCTIONS_GLSL

bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);
    float worldRadius = localSphere.w * maxScale;
    return vec4(worldCenter, worldRadius);
}

bool aabbInFrustum(vec3 aabbMin, vec3 aabbMax, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        vec3 positive = vec3(
            frustumPlanes[i].x > 0.0 ? aabbMax.x : aabbMin.x,
            frustumPlanes[i].y > 0.0 ? aabbMax.y : aabbMin.y,
            frustumPlanes[i].z > 0.0 ? aabbMax.z : aabbMin.z
        );
        float distance = dot(frustumPlanes[i].xyz, positive) + frustumPlanes[i].w;
        if (distance < 0.0) {
            return false;
        }
    }
    return true;
}

bool coneCullTest(vec4 cone, mat4 modelMatrix, vec3 cameraPos, vec3 meshletCenter) {
    if (cone.w >= 1.0) {
        return true;
    }
    vec3 worldConeAxis = normalize(mat3(modelMatrix) * cone.xyz);
    vec3 viewDir = normalize(meshletCenter - cameraPos);
    float dotProduct = dot(viewDir, worldConeAxis);
    return dotProduct < cone.w;
}

#endif // CULLING_FUNCTIONS_GLSL
