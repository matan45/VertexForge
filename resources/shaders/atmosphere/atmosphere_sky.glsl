#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "atmosphere_common.glsl"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D skyViewLUT;
layout(set = 0, binding = 1) uniform sampler2D transmittanceLUT;

layout(std140, set = 0, binding = 2) uniform AtmosphereParams {
#include "atmosphere_params.glsl"
} params;

void main()
{
    // Reconstruct view direction: unproject screen pixel to world, subtract camera pos
    vec2 ndc = texCoord * 2.0 - 1.0;
    vec4 worldTarget = params.invViewProjection * vec4(ndc, 0.0, 1.0);
    vec3 viewDir = normalize(worldTarget.xyz / worldTarget.w - params.cameraPosition.xyz);

    float altitude = params.cameraPosition.w;
    float pRadius = params.planetParams.x;
    float aRadius = params.planetParams.y;
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 sunDir = normalize(params.sunDirection.xyz);

    // Sample sky-view LUT
    vec2 skyUV = directionToSkyViewUV(viewDir, up, altitude, pRadius, aRadius);
    vec3 skyColor = texture(skyViewLUT, clamp(skyUV, vec2(0.001), vec2(0.999))).rgb;

    // Below horizon: keep the LUT ground color (scene geometry renders on top)

    // Sun disk
    float cosAngle = dot(viewDir, sunDir);
    float sunAngRad = params.sunIrradiance.w;
    if (cosAngle > cos(sunAngRad))
    {
        float edge = smoothstep(cos(sunAngRad * 1.1), cos(sunAngRad * 0.9), cosAngle);
        float cosZenith = dot(up, sunDir);
        vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT, pRadius, aRadius, altitude, cosZenith);
        float transLum = dot(transToSun, vec3(0.333));
        if (transLum < 0.0001)
            transToSun = vec3(1.0);
        skyColor += params.sunIrradiance.xyz * transToSun * edge;
    }

    // Moon disk
    vec3 moonDir = normalize(params.moonDirection.xyz);
    float moonAngRad = params.moonDirection.w;
    float moonBrightness = params.moonParams.x;
    float moonPhase = params.moonParams.y;

    float cosMoonAngle = dot(viewDir, moonDir);

    // Atmospheric glow around moon (visible beyond the disk edge)
    if (cosMoonAngle > cos(moonAngRad * 6.0) && moonBrightness > 0.0)
    {
        float glowAngle = acos(clamp(cosMoonAngle, -1.0, 1.0));
        float glowFalloff = exp(-glowAngle * glowAngle / (moonAngRad * moonAngRad * 3.0));
        float cosMoonZenith = dot(up, moonDir);
        vec3 transToMoon = sampleTransmittanceLUT(transmittanceLUT, pRadius, aRadius, altitude, cosMoonZenith);
        float moonTransLum = dot(transToMoon, vec3(0.333));
        if (moonTransLum < 0.0001)
            transToMoon = vec3(1.0);

        // Soft bluish-white glow from atmospheric scattering
        vec3 glowColor = vec3(0.7, 0.8, 1.0) * moonBrightness * 0.15;
        skyColor += glowColor * glowFalloff * transToMoon * moonPhase;
    }

    if (cosMoonAngle > cos(moonAngRad) && moonBrightness > 0.0)
    {
        float moonEdge = smoothstep(cos(moonAngRad * 1.1), cos(moonAngRad * 0.9), cosMoonAngle);

        // Moon-local coordinate frame for surface detail and phase
        vec3 moonCross = cross(moonDir, sunDir);
        float crossLen = length(moonCross);
        vec3 moonRight = (crossLen > 0.001) ? moonCross / crossLen : normalize(cross(moonDir, up));
        vec3 moonUp = cross(moonRight, moonDir);

        // UV on the moon disk surface (-1..1)
        vec3 toViewer = viewDir - moonDir * cosMoonAngle;
        float diskDist = length(toViewer);
        vec3 toViewerNorm = (diskDist > 0.0001) ? toViewer / diskDist : moonRight;
        float uvScale = diskDist / sin(moonAngRad);
        float localU = dot(toViewerNorm, moonRight) * uvScale;
        float localV = dot(toViewerNorm, moonUp) * uvScale;

        // Phase terminator (smooth transition between lit and dark side)
        float terminator = smoothstep(-0.2, 0.2, localU * (moonPhase * 2.0 - 1.0) + (moonPhase - 0.5));
        float phaseMask = mix(terminator, 1.0, smoothstep(0.9, 1.0, moonPhase));

        // Procedural moon surface using single smooth FBM (subtle variation only)
        vec2 moonUV = vec2(localU, localV);
        float surfaceNoise = 0.0;
        {
            float amp = 0.5;
            float freq = 2.0;
            vec2 seedOffset = vec2(0.0);
            for (int i = 0; i < 5; i++)
            {
                vec2 p = moonUV * freq + seedOffset;
                vec2 ip = floor(p);
                vec2 fp = fract(p);
                vec2 u = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0); // quintic smoothing
                float a = fract(sin(dot(ip,                  vec2(127.1, 311.7))) * 43758.5453);
                float b = fract(sin(dot(ip + vec2(1.0, 0.0), vec2(127.1, 311.7))) * 43758.5453);
                float c = fract(sin(dot(ip + vec2(0.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
                float d = fract(sin(dot(ip + vec2(1.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
                surfaceNoise += amp * mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
                freq *= 2.0;
                amp *= 0.5;
                seedOffset += vec2(17.3, 31.7);
            }
        }
        // Gentle surface variation: mostly bright with subtle darker patches
        float surfaceAlbedo = mix(0.82, 1.0, smoothstep(0.35, 0.65, surfaceNoise));

        // Limb darkening (edges of moon disk are darker)
        float r2 = localU * localU + localV * localV;
        float limbDarkening = 1.0 - 0.4 * r2;

        // Earthshine: faint illumination on the unlit side
        float earthshine = 0.03 * (1.0 - phaseMask);

        // Moon transmittance through atmosphere
        float cosMoonZenith = dot(up, moonDir);
        vec3 transToMoon = sampleTransmittanceLUT(transmittanceLUT, pRadius, aRadius, altitude, cosMoonZenith);
        float moonTransLum = dot(transToMoon, vec3(0.333));
        if (moonTransLum < 0.0001)
            transToMoon = vec3(1.0);

        // Warm tint to moonlight (slight yellow-white)
        vec3 moonColor = vec3(0.95, 0.93, 0.88);

        // Combine: surface * phase * limb * transmittance
        float litSurface = (phaseMask + earthshine) * surfaceAlbedo * limbDarkening;
        skyColor += moonColor * params.sunIrradiance.xyz * moonBrightness * transToMoon * moonEdge * litSurface;
    }

    // Stars (visible when sun is below horizon)
    float sunElev = dot(up, sunDir);
    float starVisibility = smoothstep(-0.05, -0.15, sunElev);

    if (starVisibility > 0.0 && viewDir.y > 0.0)
    {
        float starDensity = params.starParams.x;
        float starBright = params.starParams.y;
        float starTwinkle = params.starParams.z;
        float starTime = params.starParams.w;

        // Apparent sidereal rotation around Y axis
        float rotAngle = starTime * 0.0001;
        float cosRot = cos(rotAngle);
        float sinRot = sin(rotAngle);
        vec3 starViewDir = vec3(
            viewDir.x * cosRot - viewDir.z * sinRot,
            viewDir.y,
            viewDir.x * sinRot + viewDir.z * cosRot
        );

        // Spherical coordinates for uniform grid
        float theta = acos(clamp(starViewDir.y, -1.0, 1.0));
        float phi = atan(starViewDir.z, starViewDir.x);

        // Accumulate stars from multiple layers for depth and richness
        vec3 totalStarLight = vec3(0.0);

        // --- Layer 1: Bright primary stars (sparse, large) ---
        {
            vec2 cell = vec2(phi * 600.0, theta * 300.0);
            vec2 cellId = floor(cell);
            vec2 cellFrac = fract(cell);

            float h  = fract(sin(dot(cellId, vec2(127.1, 311.7))) * 43758.5453);
            float h2 = fract(sin(dot(cellId, vec2(269.5, 183.3))) * 43758.5453);
            float h3 = fract(sin(dot(cellId, vec2(419.2, 371.9))) * 43758.5453);
            float h4 = fract(sin(dot(cellId, vec2(541.3, 223.7))) * 43758.5453);

            if (h < starDensity * 0.4)
            {
                vec2 starPos = vec2(h2, h3) * 0.6 + 0.2;
                float dist = length(cellFrac - starPos);

                // Sharper bright core with soft glow halo
                float core = exp(-dist * dist / (0.002));
                float glow = exp(-dist * dist / (0.012));
                float starMask = core + glow * 0.3;

                // Apparent magnitude variation (some stars much brighter)
                float magnitude = mix(0.6, 2.5, h4 * h4);

                // Spectral class color: O(blue) -> B -> A(white) -> F -> G -> K -> M(red)
                vec3 starColor;
                if (h2 < 0.1)
                    starColor = vec3(0.6, 0.7, 1.0);       // O/B - hot blue
                else if (h2 < 0.35)
                    starColor = vec3(0.8, 0.85, 1.0);      // A - blue-white
                else if (h2 < 0.65)
                    starColor = vec3(1.0, 1.0, 0.95);      // F/G - white/yellow-white
                else if (h2 < 0.85)
                    starColor = vec3(1.0, 0.9, 0.7);       // K - orange
                else
                    starColor = vec3(1.0, 0.75, 0.55);     // M - red-orange

                // Slow twinkle with per-star frequency and phase
                float twinkleFreq = 0.8 + h3 * 2.5;
                float twinklePhase = h * 137.0;
                float twinkle = 0.75 + 0.25 * sin(starTime * starTwinkle * twinkleFreq + twinklePhase);
                // Second harmonic for more natural scintillation
                twinkle *= 0.85 + 0.15 * sin(starTime * starTwinkle * twinkleFreq * 1.7 + twinklePhase * 0.7);

                totalStarLight += starColor * starBright * magnitude * starMask * twinkle;
            }
        }

        // --- Layer 2: Medium stars (moderate density) ---
        {
            vec2 cell = vec2(phi * 1000.0, theta * 500.0);
            vec2 cellId = floor(cell);
            vec2 cellFrac = fract(cell);

            float h  = fract(sin(dot(cellId, vec2(173.9, 259.1))) * 43758.5453);
            float h2 = fract(sin(dot(cellId, vec2(337.1, 467.3))) * 43758.5453);
            float h3 = fract(sin(dot(cellId, vec2(521.7, 113.9))) * 43758.5453);

            if (h < starDensity * 0.8)
            {
                vec2 starPos = vec2(h2, h3) * 0.6 + 0.2;
                float dist = length(cellFrac - starPos);

                float core = exp(-dist * dist / (0.0015));
                float glow = exp(-dist * dist / (0.006));
                float starMask = core + glow * 0.15;
                float magnitude = mix(0.3, 0.8, h2);

                vec3 starColor = mix(vec3(0.85, 0.9, 1.0), vec3(1.0, 0.95, 0.85), h3);

                float twinkle = 0.8 + 0.2 * sin(starTime * starTwinkle * (1.2 + h * 2.0) + h * 89.0);

                totalStarLight += starColor * starBright * magnitude * starMask * twinkle;
            }
        }

        // --- Layer 3: Faint background stars (dense, tiny, subtle) ---
        {
            vec2 cell = vec2(phi * 2000.0, theta * 1000.0);
            vec2 cellId = floor(cell);
            vec2 cellFrac = fract(cell);

            float h  = fract(sin(dot(cellId, vec2(97.3, 431.1))) * 43758.5453);
            float h2 = fract(sin(dot(cellId, vec2(197.7, 293.5))) * 43758.5453);
            float h3 = fract(sin(dot(cellId, vec2(367.1, 571.3))) * 43758.5453);

            if (h < starDensity * 2.0)
            {
                vec2 starPos = vec2(h2, h3) * 0.6 + 0.2;
                float dist = length(cellFrac - starPos);

                float starMask = exp(-dist * dist / (0.0008));
                float magnitude = mix(0.05, 0.25, h2);

                vec3 starColor = mix(vec3(0.9, 0.92, 1.0), vec3(1.0, 0.98, 0.9), h3);

                // Faint stars twinkle less
                float twinkle = 0.9 + 0.1 * sin(starTime * starTwinkle * 0.5 + h * 200.0);

                totalStarLight += starColor * starBright * magnitude * starMask * twinkle;
            }
        }

        // Fade stars near horizon (atmospheric extinction)
        float horizonFade = smoothstep(0.0, 0.08, viewDir.y);
        skyColor += totalStarLight * starVisibility * horizonFade;
    }

    // Night sky ambient floor
    float nightFactor = smoothstep(0.0, -0.15, sunElev);
    skyColor += vec3(params.moonParams.z) * nightFactor;

    outColor = vec4(skyColor, 1.0);
}
