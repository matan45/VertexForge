#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require
// Cloud ray march compute shader - half resolution
// Marches rays through cloud layer, accumulates scattering and transmittance

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D cloudResult;
layout(set = 0, binding = 1) uniform sampler3D shapeNoiseTex;
layout(set = 0, binding = 2) uniform sampler3D detailNoiseTex;
layout(set = 0, binding = 3) uniform sampler2D weatherMapTex;
layout(set = 0, binding = 4) uniform sampler2D transmittanceLUT;

layout(set = 0, binding = 5) uniform CloudParams {
    vec4 cloudLayer;        // x=minAlt, y=maxAlt, z=thickness, w=planetRadius
    vec4 cloudDensity;      // x=globalDensity, y=globalCoverage, z=0, w=cloudType
    vec4 cloudShaping;      // x=shapeScale, y=detailScale, z=erosionStrength, w=curlStrength
    vec4 windParams;        // xyz=windDir*speed, w=timeOffset
    vec4 lightParams;       // x=lightAbsorption, y=phaseG1, z=phaseG2, w=phaseBlend
    vec4 lightColor;        // xyz=sunIrradiance, w=ambientIntensity
    vec4 sunDirection;      // xyz=sunDir, w=0
    vec4 cameraPosition;    // xyz=pos, w=0
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 screenParams;      // x=width, y=height, z=near, w=far
    vec4 temporalParams;    // x=blendFactor, y=frameIndex, z=0, w=0
    vec4 atmosphereParams;  // x=planetRadius, y=atmosphereRadius
    vec4 marchParams;       // x=maxSteps, y=lightSteps
    vec4 cloudColorTint;    // xyz=tint RGB
    vec4 lightParams2;      // x=silverLiningIntensity, y=silverLiningSpread, z=multiScatterBoost, w=0
} params;

layout(set = 0, binding = 6) uniform sampler2D blueNoiseTex;

#include "cloud_common.glsl"

// ──────────────────────────────────────────────────────────────
// Atmosphere transmittance LUT sampling
// ──────────────────────────────────────────────────────────────

vec2 transmittanceLUTParamsToUV(float planetRadius, float atmosphereRadius, float altitude, float cosZenith)
{
    float H = sqrt(max(atmosphereRadius * atmosphereRadius - planetRadius * planetRadius, 0.0));
    float rho = sqrt(max((planetRadius + altitude) * (planetRadius + altitude) - planetRadius * planetRadius, 0.0));

    float d = max(atmosphereRadius - planetRadius - altitude, 0.001);
    float dMin = (planetRadius + altitude) - planetRadius;
    float dMax = rho + H;

    float discriminant = (planetRadius + altitude) * (planetRadius + altitude) * (cosZenith * cosZenith - 1.0)
                         + atmosphereRadius * atmosphereRadius;
    float trueD = max(-((planetRadius + altitude) * cosZenith) + sqrt(max(discriminant, 0.0)), 0.0);

    float xMu = (trueD - dMin) / max(dMax - dMin, 0.001);
    float xR = rho / max(H, 0.001);

    return vec2(xMu, xR);
}

vec3 sampleTransmittanceLUT(float planetRadius, float atmosphereRadius, float altitude, float cosZenith)
{
    vec2 uv = transmittanceLUTParamsToUV(planetRadius, atmosphereRadius, altitude, cosZenith);
    uv = clamp(uv, vec2(0.001), vec2(0.999));
    return texture(transmittanceLUT, uv).rgb;
}

// ──────────────────────────────────────────────────────────────
// Cloud density sampling
// ──────────────────────────────────────────────────────────────

float sampleCloudDensity(vec3 worldPos, float heightFrac, bool detailPass)
{
    float planetRadius = params.cloudLayer.w;
    float minAlt = params.cloudLayer.x;
    float maxAlt = params.cloudLayer.y;

    // Wind displacement
    vec3 windOffset = params.windParams.xyz * params.windParams.w;
    vec3 samplePos = worldPos + windOffset;

    // Weather map (tiled across XZ plane)
    vec2 weatherUV = samplePos.xz * 0.00002 + 0.5;
    vec4 weather = texture(weatherMapTex, weatherUV);

    float weatherCoverage = weather.r;
    float weatherType = weather.g;

    // Coverage: weather map determines WHERE clouds exist,
    // globalCoverage acts as a threshold on the weather map
    // Low globalCoverage = only highest-coverage weather areas produce clouds (fewer clouds)
    // High globalCoverage = most weather areas produce clouds (more clouds)
    float coverageThreshold = 1.0 - params.cloudDensity.y;
    float localCoverage = max(weatherCoverage - coverageThreshold, 0.0) / max(params.cloudDensity.y, 0.001);
    localCoverage = clamp(localCoverage, 0.0, 1.0);

    if (localCoverage < 0.01)
        return 0.0;

    // Height gradient
    float gradient = heightGradient(heightFrac, mix(params.cloudDensity.w, weatherType, 0.5));

    // Sample shape noise (low frequency)
    vec3 shapeUV = samplePos * params.cloudShaping.x;
    vec4 shapeNoise = texture(shapeNoiseTex, shapeUV);

    // Build shape-frequency noise
    float shapeFBM = shapeNoise.g * 0.625 + shapeNoise.b * 0.25 + shapeNoise.a * 0.125;
    float shapeValue = remap(shapeNoise.r, shapeFBM - 1.0, 1.0, 0.0, 1.0);
    shapeValue = clamp(shapeValue, 0.0, 1.0);

    // Apply height gradient and coverage, then use coverage to carve out gaps
    // Subtract (1 - localCoverage) so that low-coverage areas become zero
    float baseCloud = remap(shapeValue * gradient, 1.0 - localCoverage, 1.0, 0.0, 1.0);
    float density = clamp(baseCloud, 0.0, 1.0);

    if (density < 0.01)
        return 0.0;

    // Detail noise (high frequency erosion) - only for fine march steps
    if (detailPass && density > 0.01)
    {
        // Curl noise distortion: warps the detail sample position
        // Creates turbulent, swirly shapes at cloud edges
        float curlStrength = params.cloudShaping.w;
        vec3 curlOffset = vec3(0.0);
        if (curlStrength > 0.01)
        {
            curlOffset = curlNoise(samplePos * params.cloudShaping.y * 0.5) * curlStrength * 300.0;
            // Stronger curl at cloud tops where turbulence is greater
            curlOffset *= (0.3 + 0.7 * heightFrac);
        }

        vec3 detailUV = (samplePos + curlOffset) * params.cloudShaping.y;
        vec4 detailNoise = texture(detailNoiseTex, detailUV);
        float detailFBM = detailNoise.a;

        // Erode edges (stronger at top of cloud)
        float erosionAmount = mix(0.2, 0.6, clamp(heightFrac, 0.0, 1.0)) * params.cloudShaping.z;
        density = max(density - detailFBM * erosionAmount, 0.0);

        // Extra detail octave at close range for wispy edges
        float cameraDist = length(worldPos - params.cameraPosition.xyz);
        if (cameraDist < 5000.0)
        {
            vec3 fineUV = (samplePos + curlOffset * 0.5) * params.cloudShaping.y * 2.0;
            float fineDetail = texture(detailNoiseTex, fineUV).a;
            float closeBlend = 1.0 - smoothstep(2000.0, 5000.0, cameraDist);
            density = max(density - fineDetail * erosionAmount * 0.3 * closeBlend, 0.0);
        }
    }

    return density * params.cloudDensity.x;
}

// ──────────────────────────────────────────────────────────────
// Light march (towards sun)
// ──────────────────────────────────────────────────────────────

float lightMarch(vec3 pos, float heightFrac)
{
    float planetRadius = params.cloudLayer.w;
    float minAlt = params.cloudLayer.x;
    float maxAlt = params.cloudLayer.y;
    vec3 sunDir = normalize(params.sunDirection.xyz);

    int steps = int(params.marchParams.y);
    float stepSize = (maxAlt - minAlt) / float(steps);

    float totalDensity = 0.0;
    vec3 marchPos = pos;

    for (int i = 0; i < steps; ++i)
    {
        marchPos += sunDir * stepSize;

        float altitude = length(marchPos - vec3(0.0, -planetRadius, 0.0)) - planetRadius;
        float hf = getHeightFraction(altitude, minAlt, maxAlt);

        if (hf < 0.0 || hf > 1.0)
            break;

        float d = sampleCloudDensity(marchPos, hf, false);
        totalDensity += d * stepSize;
    }

    return totalDensity;
}

// ──────────────────────────────────────────────────────────────
// Main ray march
// ──────────────────────────────────────────────────────────────

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 halfSize = ivec2(params.screenParams.x * 0.5, params.screenParams.y * 0.5);
    if (any(greaterThanEqual(texel, halfSize)))
        return;

    // Checkerboard pattern: only march 1/4 pixels per frame
    uint frameIndex = uint(params.temporalParams.y);
    uint checkerboard = (texel.x + texel.y * 2 + frameIndex) % 4;

    // UV for this half-res pixel
    vec2 uv = (vec2(texel) + 0.5) / vec2(halfSize);

    // Reconstruct world-space ray
    vec4 clipPos = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vec4 worldPos4 = params.invViewProjection * clipPos;
    vec3 worldPos = worldPos4.xyz / worldPos4.w;
    vec3 rayOrigin = params.cameraPosition.xyz;
    vec3 rayDir = normalize(worldPos - rayOrigin);

    // Planet center (camera is on surface, planet center below)
    float planetRadius = params.cloudLayer.w;
    vec3 planetCenter = vec3(0.0, -planetRadius, 0.0);

    // Skip rays that can't reach the cloud layer
    vec3 surfaceNormal = normalize(rayOrigin - planetCenter);
    float horizonDot = dot(rayDir, surfaceNormal);
    float cameraAltitude = length(rayOrigin - planetCenter) - planetRadius;

    // Below cloud layer: only rays going upward can see clouds
    // Above cloud layer: rays going downward can also see clouds
    float minDot = (cameraAltitude < params.cloudLayer.x) ? -0.01 : -0.5;
    if (horizonDot < minDot)
    {
        imageStore(cloudResult, texel, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    float horizonFade = smoothstep(minDot, minDot + 0.05, horizonDot);

    // Intersect ray with cloud layer spheres
    float innerRadius = planetRadius + params.cloudLayer.x;
    float outerRadius = planetRadius + params.cloudLayer.y;

    vec2 innerHit = raySphereIntersect(rayOrigin, rayDir, planetCenter, innerRadius);
    vec2 outerHit = raySphereIntersect(rayOrigin, rayDir, planetCenter, outerRadius);

    // Determine march start/end
    float marchStart, marchEnd;

    if (cameraAltitude < params.cloudLayer.x)
    {
        // Below cloud layer
        if (innerHit.y < 0.0)
        {
            imageStore(cloudResult, texel, vec4(0.0, 0.0, 0.0, 1.0));
            return;
        }
        marchStart = innerHit.y;
        marchEnd = outerHit.y;
    }
    else if (cameraAltitude > params.cloudLayer.y)
    {
        // Above cloud layer
        if (outerHit.x < 0.0)
        {
            imageStore(cloudResult, texel, vec4(0.0, 0.0, 0.0, 1.0));
            return;
        }
        marchStart = outerHit.x;
        marchEnd = innerHit.x > 0.0 ? innerHit.x : outerHit.y;
    }
    else
    {
        // Inside cloud layer
        marchStart = 0.0;
        marchEnd = outerHit.y;
    }

    if (marchEnd <= marchStart || marchStart < 0.0)
    {
        imageStore(cloudResult, texel, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    // Clamp max distance
    float maxDist = 50000.0;
    marchEnd = min(marchEnd, marchStart + maxDist);

    // Ray march parameters
    int maxSteps = int(params.marchParams.x);
    float thickness = marchEnd - marchStart;
    float baseStepSize = thickness / float(maxSteps);

    // Large initial step until we hit cloud, then fine steps
    float coarseStep = max(baseStepSize * 3.0, 100.0);
    float fineStep = baseStepSize;

    vec3 sunDir = normalize(params.sunDirection.xyz);
    float cosTheta = dot(rayDir, sunDir);
    float phase = dualLobePhase(cosTheta,
                                 params.lightParams.y,
                                 params.lightParams.z,
                                 params.lightParams.w);

    vec3 scattering = vec3(0.0);
    float transmittance = 1.0;
    float t = marchStart;
    bool inCloud = false;
    int zeroCount = 0;

    // Blue noise dithering to break banding artifacts
    vec2 blueNoiseUV = (vec2(texel) + vec2(frameIndex % 8, frameIndex / 8 % 8) * 17.0) / 128.0;
    float blueNoise = texture(blueNoiseTex, blueNoiseUV).r;
    t += blueNoise * fineStep;

    for (int i = 0; i < maxSteps && t < marchEnd; ++i)
    {
        vec3 samplePos = rayOrigin + rayDir * t;
        float altitude = length(samplePos - planetCenter) - planetRadius;
        float heightFrac = getHeightFraction(altitude, params.cloudLayer.x, params.cloudLayer.y);

        float density = sampleCloudDensity(samplePos, heightFrac, inCloud);

        if (density > 0.01)
        {
            inCloud = true;
            zeroCount = 0;
            float stepSize = fineStep;

            // Light march towards sun
            float lightDensity = lightMarch(samplePos, heightFrac);
            float absorption = params.lightParams.x;

            // Beer-Lambert with powder effect for direct light
            float beer = exp(-lightDensity * absorption);
            float powder = 1.0 - exp(-lightDensity * absorption * 2.0);
            float lightTransmittance = max(beer, powder * 0.07);

            // Multi-scattering approximation (Wrenninge/Schneider method)
            // Each bounce: extinction halves, contribution halves
            // This brightens thick cloud interiors instead of going dark
            vec3 multiScatterLight = vec3(0.0);
            float msExtinction = 1.0;
            float msEccentricity = 1.0;
            for (int ms = 0; ms < 3; ms++)
            {
                float msPhase = dualLobePhase(cosTheta,
                    params.lightParams.y * msEccentricity,
                    params.lightParams.z * msEccentricity,
                    params.lightParams.w);
                float msBeer = exp(-lightDensity * absorption * msExtinction);
                multiScatterLight += vec3(msPhase * msBeer * msExtinction);
                msExtinction *= 0.5;
                msEccentricity *= 0.5; // each bounce becomes more isotropic
            }

            // Interior boost: brightens deeply embedded cloud samples
            // Uses lightDensity (accumulated toward sun) — high when deep inside cloud
            float interiorBoost = (1.0 - exp(-lightDensity * absorption * 0.5)) * params.lightParams2.z;
            multiScatterLight += vec3(interiorBoost * 0.1);

            // Sample atmosphere transmittance for sun at this altitude
            float cosZenith = dot(normalize(samplePos - planetCenter), sunDir);
            vec3 sunTransmittance = vec3(1.0);
            if (params.atmosphereParams.x > 0.0)
            {
                sunTransmittance = sampleTransmittanceLUT(
                    params.atmosphereParams.x,
                    params.atmosphereParams.y,
                    altitude, cosZenith);
            }

            // Sun illumination with multi-scattering
            vec3 sunColor = params.lightColor.xyz * 3.5;
            vec3 directSun = sunColor * sunTransmittance * multiScatterLight;

            // Silver lining: bright rim when looking toward sun through thin cloud edges
            if (params.lightParams2.x > 0.01)
            {
                float edgeFactor = exp(-lightDensity * absorption * 0.5);
                float silverPhase = pow(max(cosTheta, 0.0), params.lightParams2.y);
                directSun += sunColor * sunTransmittance * edgeFactor * silverPhase * params.lightParams2.x;
            }

            // Ambient: sky light from all directions
            // Brighter at cloud tops, darker/bluer at bases
            vec3 skyColor = sunColor * vec3(0.4, 0.5, 0.7); // blue-tinted sky ambient
            vec3 groundBounce = sunColor * vec3(0.15, 0.12, 0.1); // warm ground bounce
            vec3 ambient = mix(groundBounce, skyColor, heightFrac) * params.lightColor.w;

            vec3 lightIntensity = directSun + ambient;

            // Energy-conserving integration
            float sigmaE = density * absorption;
            float sigmaS = sigmaE * 0.9; // scattering albedo ~0.9
            float stepExtinction = sigmaE * stepSize;
            float stepTransmittance = exp(-stepExtinction);
            vec3 stepScattering = sigmaS * lightIntensity * (1.0 - stepTransmittance) / max(sigmaE, 0.0001);

            scattering += transmittance * stepScattering;
            transmittance *= stepTransmittance;

            if (transmittance < 0.01)
                break;

            t += stepSize;
        }
        else
        {
            zeroCount++;
            if (zeroCount > 3)
                inCloud = false;

            t += inCloud ? fineStep : coarseStep;
        }
    }

    // Apply color tint
    scattering *= params.cloudColorTint.xyz;

    // Distance fade: distant clouds fade out (atmosphere handles the haze)
    float marchDist = max(t - marchStart, 0.0);
    float distanceFade = exp(-marchDist * 0.00003);
    scattering *= mix(distanceFade, 1.0, 0.3); // keep 30% minimum
    transmittance = mix(1.0, transmittance, mix(distanceFade, 1.0, 0.3));

    // Apply horizon fade
    scattering *= horizonFade;
    transmittance = mix(1.0, transmittance, horizonFade);

    imageStore(cloudResult, texel, vec4(scattering, transmittance));
}
