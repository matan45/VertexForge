#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragOceanDispY;  // ocean wave height displacement
layout(location = 4) out float fragBaseHeight;   // flat water height (no waves)
// VK-1605: sampled from the shore-depth field at the UNDISPLACED vertex position. The fragment
// stage cannot sample it usefully on its own (it only has a screen-space depth reconstruction),
// and interpolating is both cheaper and consistent with the geometry that was actually displaced.
layout(location = 5) out float fragShoreDepth;
layout(location = 6) out float fragShoreFade;
layout(location = 7) out float fragShoalFactor;  // band 0's shoaling scale, for break foam

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

struct WaterTileData {
    vec4 worldOriginAndSize;   // xyz = plane world origin, w = planeSize
    vec4 heightAndWave;        // x = waterHeight, y = 1.0, z = 0, w = 0
};
layout(set = 1, binding = 0) readonly buffer TileBuffer {
    WaterTileData tiles[];
} tileData;

layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float maxVisibleDepth;
    float fresnelPower;
    float oceanChoppiness;
    float oceanPatchSize0;       // Swell band patch size
    float oceanFoamThreshold;
    float refractionStrength;
    float refractionChromatic;
    float refractionDepthScale;
    float oceanPatchSize1;       // Agitation band patch size
    float oceanPatchSize2;       // Ripples band patch size
    uint  bandEnableMask;        // bit 0=swell, bit 1=agitation, bit 2=ripples
    float shoreFoamRange;
    float shoreFoamIntensity;
    float shoreBreakingStrength;
    uint  viewFlagMask;          // VK-1604: ANDed with ext.flags; RTT views clear SSR/absorption
} pc;

// Multi-band ocean textures (set 8)
layout(set = 8, binding = 0) uniform sampler2D oceanDisp0;   // Swell displacement
layout(set = 8, binding = 1) uniform sampler2D oceanNorm0;   // Swell normals
layout(set = 8, binding = 2) uniform sampler2D oceanDisp1;   // Agitation displacement
layout(set = 8, binding = 3) uniform sampler2D oceanNorm1;   // Agitation normals
layout(set = 8, binding = 4) uniform sampler2D oceanDisp2;   // Ripples displacement
layout(set = 8, binding = 5) uniform sampler2D oceanNorm2;   // Ripples normals

#include "water_params.glsl"
#include "hex_tiling.glsl"
#include "water_shoaling.glsl"
#include "water_ripple.glsl"

// VK-1605: scale one band's displacement for the water depth it is standing in. Vertical gets the
// Green's-law gain capped by the breaking limit, horizontal chop gets compressed so the shear
// cannot fold the mesh through the beach. Must stay in step with
// GPUDrivenRenderer::getOceanHeightAt, which applies the identical scale to the CPU band sum.
// Returns the vertical scale so the caller can forward it to the fragment stage without paying
// for a second evaluation (shoalingScale costs a pow, a tanh and a sinh).
float applyBandShoaling(inout vec4 disp, float depth, float wavelength, float strength) {
    float s = shoalingScale(depth, wavelength, disp.y, strength);
    disp.y  *= s;
    disp.xz *= chopCompression(depth, wavelength, strength);
    return s;
}

void main() {
    uint tileIndex = gl_InstanceIndex;
    vec4 originSize = tileData.tiles[tileIndex].worldOriginAndSize;
    vec4 heightWave = tileData.tiles[tileIndex].heightAndWave;

    vec3 tileOrigin = originSize.xyz;
    float tileSize = originSize.w;
    float waterHeight = heightWave.x;

    vec3 worldPos = vec3(
        tileOrigin.x + inPosition.x * tileSize,
        waterHeight,
        tileOrigin.z + inPosition.z * tileSize
    );

    fragBaseHeight = waterHeight;

    // Per-tile simulation LOD: skip expensive bands at coarse LODs
    uint lodLevel = uint(heightWave.z);
    uint tileBandMask = pc.bandEnableMask;
    if (lodLevel >= 2u) tileBandMask &= ~4u;  // skip ripples at LOD2+
    if (lodLevel >= 3u) tileBandMask &= ~2u;  // skip agitation at LOD3

    // Multi-band FFT displacement: each band at its own patch size
    vec4 totalDisp = vec4(0.0);
    vec3 totalNorm = vec3(0.0, 1.0, 0.0);

    // VK-1604: hex tile-and-blend breaks the periodic repeat of each band's patch. Per band,
    // because it costs 3x the samples. Displacement and normals MUST share the same blend or
    // the shading would describe a different surface than the geometry.
    uint hexMask = ((ext.flags & pc.viewFlagMask & WATER_FLAG_HEX) != 0u) ? ext.hexPerBandMask : 0u;

    // VK-1605: the shore-depth field is sampled at the UNDISPLACED position — the same place the
    // band UVs are derived from, and the same place the CPU height sampler is queried, so the two
    // agree. Sampling after displacement would make the depth depend on the wave it is scaling.
    //
    // effectiveFlags is uniform across the draw. Note the RTT mask does NOT clear these bits: a
    // reflection probe must displace its water exactly like the main view or it reflects a surface
    // that is not there (same rule as WATER_FLAG_HEX).
    uint vertFlags = ext.flags & pc.viewFlagMask;
    bool shoalingOn = (vertFlags & WATER_FLAG_SHOALING) != 0u;
    bool shoreWavesOn = (vertFlags & WATER_FLAG_SHORE_WAVES) != 0u;

    vec2 baseXZ = worldPos.xz;
    float shoreDepth = SHORE_FIELD_DEEP;
    float shoreFade = 0.0;
    if (shoalingOn || shoreWavesOn) {
        shoreDepth = shoreDepthAt(baseXZ);
        shoreFade = shoreWindowFade(baseXZ);
    }
    // Fading the STRENGTH (rather than the result) is what guarantees the window border is exactly
    // neutral: shoalingScale/chopCompression both early-out to literal 1.0 at strength 0.
    float shoalStrength = shoalingOn ? ext.shoalingStrength * shoreFade : 0.0;
    float shoalFactor0 = 1.0;

    // Band 0: Swell (large-scale distant wind waves)
    if ((tileBandMask & 1u) != 0u) {
        vec2 uv0 = worldPos.xz / pc.oceanPatchSize0;
        vec4 disp0;
        if (hexBandEnabled(hexMask, 0u)) {
            HexBlend hb = hexComputeBlend(uv0, ext.hexCellScale0, ext.hexBlendExponent);
            disp0 = hexSampleDisplacementLod(oceanDisp0, hb);
            totalNorm = hexSampleNormalLod(oceanNorm0, hb);
        } else {
            disp0 = texture(oceanDisp0, uv0);
            totalNorm = texture(oceanNorm0, uv0).xyz;
        }
        if (shoalStrength > 0.0)
            shoalFactor0 = applyBandShoaling(disp0, shoreDepth, ext.bandWavelength.x, shoalStrength);
        totalDisp += disp0;
    }

    // Band 1: Agitation (mid-frequency wind chaos)
    if ((tileBandMask & 2u) != 0u) {
        vec2 uv1 = worldPos.xz / pc.oceanPatchSize1;
        vec4 disp1;
        vec3 n1;
        if (hexBandEnabled(hexMask, 1u)) {
            HexBlend hb = hexComputeBlend(uv1, ext.hexCellScale1, ext.hexBlendExponent);
            disp1 = hexSampleDisplacementLod(oceanDisp1, hb);
            n1 = hexSampleNormalLod(oceanNorm1, hb);
        } else {
            disp1 = texture(oceanDisp1, uv1);
            n1 = texture(oceanNorm1, uv1).xyz;
        }
        if (shoalStrength > 0.0)
            applyBandShoaling(disp1, shoreDepth, ext.bandWavelength.y, shoalStrength);
        totalDisp += disp1;
        totalNorm = normalize(totalNorm + (n1 - vec3(0.0, 1.0, 0.0)));
    }

    // Band 2: Ripples (fine surface detail)
    if ((tileBandMask & 4u) != 0u) {
        vec2 uv2 = worldPos.xz / pc.oceanPatchSize2;
        vec4 disp2;
        vec3 n2;
        if (hexBandEnabled(hexMask, 2u)) {
            HexBlend hb = hexComputeBlend(uv2, ext.hexCellScale2, ext.hexBlendExponent);
            disp2 = hexSampleDisplacementLod(oceanDisp2, hb);
            n2 = hexSampleNormalLod(oceanNorm2, hb);
        } else {
            disp2 = texture(oceanDisp2, uv2);
            n2 = texture(oceanNorm2, uv2).xyz;
        }
        if (shoalStrength > 0.0)
            applyBandShoaling(disp2, shoreDepth, ext.bandWavelength.z, shoalStrength);
        totalDisp += disp2;
        totalNorm = normalize(totalNorm + (n2 - vec3(0.0, 1.0, 0.0)));
    }

    worldPos.x += totalDisp.x;
    worldPos.y += totalDisp.y;
    worldPos.z += totalDisp.z;

    // VK-1605: the breaking-wave deformer rides on top of the FFT. fragOceanDispY deliberately
    // keeps its FFT-only value so the existing height-based colour ramp is unchanged.
    if (shoreWavesOn) {
        float surge = shoreWaveHeight(shoreDepth, camera.u_Time) * shoreFade;
        if (surge > 0.0) {
            worldPos.y += surge;

            // Lean the crest shoreward. The depth gradient points out to sea, so -g is the
            // direction the water is running. Two extra taps, only on the vertices that carry a
            // surge at all.
            float eps = ext.shoreFieldOrigin.z * (1.0 / SHORE_FIELD_RESOLUTION);
            vec2 g = shoreDepthGradientFwd(baseXZ, shoreDepth, eps);
            float gl = length(g);
            if (gl > 1.0e-5)
                worldPos.xz -= (g / gl) * (surge * ext.shoreWaveB.w);
        }
    }

    // VK-1606: the interactive ripple patch rides on top of everything else. Sampled at the
    // UNDISPLACED position for the same reason the shore field is, and like the shore surge it
    // deliberately leaves fragOceanDispY on its FFT-only value so the existing height-based colour
    // ramp is unchanged. RTT views keep this bit: the patch is view-independent, and a probe whose
    // water sat at a different height would reflect a surface that is not there.
    if ((vertFlags & WATER_FLAG_RIPPLES) != 0u) {
        vec4 ripple = rippleSampleRaw(baseXZ);
        float rippleFade = rippleWindowFade(baseXZ);
        if (rippleFade > 0.0) {
            worldPos.y += ripple.x * ext.rippleParams.x * rippleFade;
            totalNorm = normalize(totalNorm +
                                  vec3(ripple.y, 0.0, ripple.z) * (ext.rippleParams.y * rippleFade));
        }
    }

    fragOceanDispY = totalDisp.y;
    fragNormal = normalize(totalNorm);

    fragShoreDepth = shoreDepth;
    fragShoreFade = shoreFade;
    fragShoalFactor = shoalFactor0;

    fragWorldPos = worldPos;
    fragTexCoord = inTexCoord;

    gl_Position = camera.projection * camera.view * vec4(worldPos, 1.0);
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragOceanDispY;
layout(location = 4) in float fragBaseHeight;
layout(location = 5) in float fragShoreDepth;    // VK-1605: true vertical water depth
layout(location = 6) in float fragShoreFade;
layout(location = 7) in float fragShoalFactor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

#include "../common/lighting_functions.glsl"
#include "../common/cluster_culling.glsl"

layout(std430, set = 3, binding = 0) readonly buffer DirectionalLightBuffer { DirectionalLight directionalLights[]; };
layout(std430, set = 3, binding = 1) readonly buffer PointLightBuffer { PointLight pointLights[]; };
layout(std430, set = 3, binding = 2) readonly buffer SpotLightBuffer { SpotLight spotLights[]; };
layout(std140, set = 3, binding = 3) uniform LightCountsUBO { LightCounts lightCounts; };

layout(std140, set = 4, binding = 0) uniform ClusterParamsUBO { ClusterGridParams clusterParams; };

layout(std430, set = 5, binding = 0) readonly buffer ClusterLightGridBuffer { ClusterLightData clusterLightGrid[]; };
layout(std430, set = 5, binding = 1) readonly buffer ClusterLightIndexListBuffer { uint lightIndexList[]; };

#include "../common/shadow_sampling_types.glsl"

layout(std430, set = 6, binding = 0) readonly buffer ShadowDataBuffer { ShadowData shadowDataArray[]; };

layout(std430, set = 6, binding = 1) readonly buffer PageTableBuffer { uint pageTableData[]; };

// Comparison samplers (shadow filtering)
layout(set = 7, binding = 0) uniform sampler2DShadow physicalPoolShadow;
layout(set = 7, binding = 1) uniform sampler2D physicalPoolDepth;

#define SHADOW_BUFFER shadowDataArray
#define PAGE_TABLE pageTableData
#include "../common/shadow_sampling.glsl"

// Multi-band ocean textures (set 8)
layout(set = 8, binding = 0) uniform sampler2D frag_oceanDisp0;
layout(set = 8, binding = 1) uniform sampler2D frag_oceanNorm0;
layout(set = 8, binding = 2) uniform sampler2D frag_oceanDisp1;
layout(set = 8, binding = 3) uniform sampler2D frag_oceanNorm1;
layout(set = 8, binding = 4) uniform sampler2D frag_oceanDisp2;
layout(set = 8, binding = 5) uniform sampler2D frag_oceanNorm2;

layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float maxVisibleDepth;
    float fresnelPower;
    float oceanChoppiness;
    float oceanPatchSize0;
    float oceanFoamThreshold;
    float refractionStrength;
    float refractionChromatic;
    float refractionDepthScale;
    float oceanPatchSize1;
    float oceanPatchSize2;
    uint  bandEnableMask;
    float shoreFoamRange;
    float shoreFoamIntensity;
    float shoreBreakingStrength;
    uint  viewFlagMask;          // VK-1604: ANDed with ext.flags; RTT views clear SSR/absorption
} pc;

layout(set = 9, binding = 0) uniform sampler2D refractionColorTex;
// VK-1604: the real scene depth image (was wired to the color copy before this story).
layout(set = 9, binding = 1) uniform sampler2D sceneDepthTex;

#include "water_params.glsl"
#include "hex_tiling.glsl"
#include "water_ssr.glsl"
#include "water_shoaling.glsl"
#include "water_ripple.glsl"

void main() {
    // VK-1604: features are gated by the ocean settings AND by the view. RTT / reflection-probe
    // views clear the SSR and absorption bits because set 9 holds the MAIN view's scene colour
    // copy and depth image, and because a view-dependent reflection baked into a probe cubemap
    // would be wrong from every direction except the capture one.
    uint effectiveFlags = ext.flags & pc.viewFlagMask;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 R = reflect(-V, N);

    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, pc.fresnelPower);
    fresnel = clamp(fresnel, 0.0, 1.0);

    float roughness = 0.05;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;

    // VK-1604: SSR replaces the prefiltered IBL estimate BEFORE the BRDF, not after it. That way
    // Fresnel, the ambient shadow factor and the foam overlay keep applying uniformly to both,
    // and confidence -> 0 collapses to exactly the previous expression. Blending further down
    // (at the mix() with baseColor) would apply Fresnel twice and let reflections sit on top of
    // foam, which reads as a decal.
    if ((effectiveFlags & WATER_FLAG_SSR) != 0u)
    {
        vec2 screenSize = vec2(textureSize(sceneDepthTex, 0));
        vec2 pixelUV = gl_FragCoord.xy / screenSize;
        vec3 fragViewPos = waterSSRViewPosFromDepth(pixelUV, gl_FragCoord.z);
        vec3 reflectDirView = normalize(mat3(camera.view) * R);

        WaterSSRResult ssr = traceWaterSSR(fragViewPos, reflectDirView);
        prefilteredColor = mix(prefilteredColor, ssr.color, ssr.confidence);

        if ((effectiveFlags & WATER_FLAG_SSR_DEBUG) != 0u)
        {
            outColor = vec4(vec3(ssr.confidence), 1.0);
            return;
        }
    }

    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 F0 = vec3(0.02); // Water IOR ~1.33
    vec3 specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    // Height-based color: wave peaks get shallow color, troughs get deep color
    float heightRange = fragOceanDispY;
    float heightFactor = clamp(heightRange * 0.5 + 0.5, 0.0, 1.0);
    vec3 waterColor = mix(pc.deepColor.rgb, pc.shallowColor.rgb, heightFactor);

    // Subsurface scattering approximation
    vec3 sssColor = vec3(0.0, 0.7, 0.6);
    vec3 L = normalize(vec3(0.5, 0.7, 0.3));
    vec3 lightColor = vec3(1.0);
    if (lightCounts.directionalCount > 0u) {
        L = normalize(-directionalLights[0].direction);
        lightColor = directionalLights[0].color;
    }
    float LdotV = max(dot(L, -V), 0.0);
    float waveHeight = clamp(heightRange, 0.0, 1.0);
    float NdotL = dot(N, L);
    float wrapDiffuse = max(0.0, (NdotL + 0.5) / 1.5);
    float sss = pow(LdotV, 4.0) * waveHeight * 0.8 + wrapDiffuse * waveHeight * 0.3;
    sss = clamp(sss, 0.0, 1.0);
    waterColor = mix(waterColor, sssColor * lightColor, sss);

    fresnel = clamp(fresnel, 0.02, 0.6);

    float metallic = 0.0;
    float directRoughness = 0.3;
    vec3 albedo = waterColor;

    vec3 directLighting = vec3(0.0);
    float minShadow = 1.0;

    float linearZ = linearizeDepth(clusterParams, gl_FragCoord.z);
    uint clusterIdx = getClusterIndex(clusterParams, gl_FragCoord.xy, linearZ);

    // VK-1604: reconstruct the scene depth behind this water pixel ONCE. Both Beer-Lambert
    // absorption and the shore-foam block below need it, and it used to be computed inside the
    // shore-foam block (which is gated on shoreFoamRange > 0), so it was unavailable elsewhere.
    vec2 sceneScreenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthTex, 0));
    float sceneRawDepth = texture(sceneDepthTex, sceneScreenUV).r;
    float sceneLinearZ = linearizeDepth(clusterParams, sceneRawDepth);
    bool sceneIsSky = sceneRawDepth >= 1.0;
    // View-space depth difference scaled by view angle -> approximate vertical water depth.
    float cosViewAngle = max(abs(dot(normalize(camera.cameraPos - fragWorldPos), vec3(0.0, 1.0, 0.0))), 0.1);
    float sceneVerticalDepth = max((sceneLinearZ - linearZ) * cosViewAngle, 0.0);

    if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
        ClusterLightData clusterData = clusterLightGrid[clusterIdx];
        uint clusterPointCount = getClusterPointLightCount(clusterData);
        uint clusterSpotCount = getClusterSpotLightCount(clusterData);
        uint lightOffset = clusterData.offset;

        for (uint i = 0u; i < clusterPointCount; ++i) {
            uint lightIdx = lightIndexList[lightOffset + i];
            PointLight light = pointLights[lightIdx];

            float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, N,
                                             light.position, light.radius);
            minShadow = min(minShadow, shadow);

            directLighting += evaluatePointLight(fragWorldPos, N, V, albedo,
                                                 metallic, directRoughness, F0, light) * shadow;
        }

        for (uint i = 0u; i < clusterSpotCount; ++i) {
            uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
            uint lightIdx = extractLightIndex(packedIdx);
            SpotLight light = spotLights[lightIdx];

            float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos, N);
            minShadow = min(minShadow, shadow);

            directLighting += evaluateSpotLight(fragWorldPos, N, V, albedo,
                                                metallic, directRoughness, F0, light) * shadow;
        }
    }

    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];

        float shadow = 1.0; // Directional lights use RT shadows, not VSM
        minShadow = min(minShadow, shadow);

        vec3 lightContrib = evaluateDirectionalLight(N, V, albedo, metallic, directRoughness, F0, light);
        directLighting += lightContrib * shadow;
    }

    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);

    // Refraction (samples scene color captured before water pass)
    vec3 refractionColor = waterColor;
    if (pc.refractionStrength > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(refractionColorTex, 0));

        float viewAngleFactor = 1.0 - abs(dot(V, vec3(0.0, 1.0, 0.0)));
        float depthFactor = smoothstep(0.0, 1.0, viewAngleFactor * pc.refractionDepthScale);

        vec2 distortion = N.xz * pc.refractionStrength * depthFactor * 0.1;
        vec2 refractedUV = clamp(screenUV + distortion, vec2(0.001), vec2(0.999));

        if (pc.refractionChromatic > 0.0) {
            float spread = pc.refractionChromatic * depthFactor * 0.002;
            vec2 chromaticDir = normalize(distortion + vec2(0.001));
            float r = texture(refractionColorTex, refractedUV + chromaticDir * spread).r;
            float g = texture(refractionColorTex, refractedUV).g;
            float b = texture(refractionColorTex, refractedUV - chromaticDir * spread).b;
            refractionColor = vec3(r, g, b);
        } else {
            refractionColor = texture(refractionColorTex, refractedUV).rgb;
        }

        // VK-1604: Beer-Lambert absorption + in-scattering, or the legacy view-angle tint.
        // The else branch is the original two lines MOVED VERBATIM: any algebraic restructuring
        // could shift the last ULP and break the "flag off == byte-identical" guarantee.
        // effectiveFlags is uniform across the draw, so this branch never diverges.
        //
        // sceneIsSky matters: at the horizon there is no geometry behind the water, sceneLinearZ
        // saturates to the far plane, the path length clamps to its maximum and transmittance
        // goes to zero — a black band across the whole horizon. Fall back to the legacy tint.
        if ((effectiveFlags & WATER_FLAG_ABSORPTION) != 0u && !sceneIsSky) {
            // Light travels down to the sea floor and back up to the viewer, so the path is
            // longer than the vertical depth — and increasingly so at grazing angles, which is
            // exactly where water is usually seen.
            float pathLength = clamp(sceneVerticalDepth * (1.0 + 1.0 / max(NdotV, 0.1)) * 0.5,
                                      0.0, ext.absorptionMaxDistance);
            vec3 transmittance = exp(-ext.absorptionCoeff.rgb * pathLength);
            vec3 inscatter = ext.scatterColor.rgb * (1.0 - exp(-ext.scatterCoeff.rgb * pathLength));
            refractionColor = refractionColor * transmittance + inscatter;
        } else {
            float depthTint = smoothstep(0.0, 1.0, viewAngleFactor);
            refractionColor = mix(refractionColor, waterColor, depthTint * 0.5);
        }
    }

    vec3 baseColor = (pc.refractionStrength > 0.0) ? refractionColor : waterColor;
    vec3 color = mix(baseColor, specular, fresnel) * ambientShadowFactor + directLighting;

    // Ocean foam blending — sum foam from all active bands
    // VK-1604: hex-tiled bands must have their foam tiled the same way, or the foam pattern
    // keeps repeating on a surface whose geometry no longer does. Derivatives are taken from the
    // CONTINUOUS (un-offset) UV: the per-cell offsets are discontinuous across cell edges, so
    // implicit derivatives would spike there and produce a blurred grid of seams plus shimmer
    // under motion — the hazard documented for triplanar sampling in
    // material/terrain_material_generated.glsl (VK-1209).
    // Note these UVs use the DISPLACED fragWorldPos while the vertex stage uses the undisplaced
    // position; that mismatch predates VK-1604 and is left alone (foam is an independent layer,
    // and unifying it would change existing content for no benefit).
    uint fragHexMask = ((effectiveFlags & WATER_FLAG_HEX) != 0u) ? ext.hexPerBandMask : 0u;

    float foam = 0.0;
    if ((pc.bandEnableMask & 1u) != 0u) {
        vec2 foamUV0 = fragWorldPos.xz / pc.oceanPatchSize0;
        if (hexBandEnabled(fragHexMask, 0u)) {
            HexBlend hb = hexComputeBlend(foamUV0, ext.hexCellScale0, ext.hexBlendExponent);
            foam += hexSampleFoamGrad(frag_oceanDisp0, hb, dFdx(foamUV0), dFdy(foamUV0));
        } else {
            foam += texture(frag_oceanDisp0, foamUV0).w;
        }
    }
    if ((pc.bandEnableMask & 2u) != 0u) {
        vec2 foamUV1 = fragWorldPos.xz / pc.oceanPatchSize1;
        if (hexBandEnabled(fragHexMask, 1u)) {
            HexBlend hb = hexComputeBlend(foamUV1, ext.hexCellScale1, ext.hexBlendExponent);
            foam += hexSampleFoamGrad(frag_oceanDisp1, hb, dFdx(foamUV1), dFdy(foamUV1)) * 0.5;
        } else {
            foam += texture(frag_oceanDisp1, foamUV1).w * 0.5;
        }
    }
    if ((pc.bandEnableMask & 4u) != 0u) {
        vec2 foamUV2 = fragWorldPos.xz / pc.oceanPatchSize2;
        if (hexBandEnabled(fragHexMask, 2u)) {
            HexBlend hb = hexComputeBlend(foamUV2, ext.hexCellScale2, ext.hexBlendExponent);
            foam += hexSampleFoamGrad(frag_oceanDisp2, hb, dFdx(foamUV2), dFdy(foamUV2)) * 0.3;
        } else {
            foam += texture(frag_oceanDisp2, foamUV2).w * 0.3;
        }
    }

    // VK-1605: breaking-wave crest foam. World-space and shore-anchored, so it deliberately does
    // NOT go through the per-band advected foam ping-pong (that buffer is periodic patch space and
    // cannot host it). Faded at the shore-field window border like every other shoreline effect.
    if ((effectiveFlags & WATER_FLAG_SHORE_WAVES) != 0u) {
        foam += shoreCrestFoam(fragShoreDepth, camera.u_Time) * fragShoreFade;
    }

    // VK-1606: wake and splash foam. Sampled per-pixel from set 9 b4 rather than interpolated from
    // the vertex stage - the varying slots are full (locations 0..7) and a wake trail is finer than
    // the water mesh, so a per-vertex value would visibly quantise it to the tessellation.
    if ((effectiveFlags & WATER_FLAG_RIPPLES) != 0u) {
        foam += rippleSampleRaw(fragWorldPos.xz).w * ext.rippleParams.z * rippleWindowFade(fragWorldPos.xz);
    }

    // Shore foam — depth-based foam where water meets terrain
    if (pc.shoreFoamRange > 0.0) {
        // VK-1604: reuses the hoisted reconstruction above. sceneDepthTex is now the real scene
        // depth image (set 9 b1) — before VK-1604 the descriptor pointed at the scene *color*
        // copy, so this linearized the red channel and shore foam tracked scene brightness
        // rather than distance to geometry.
        //
        // VK-1605: once the shore-depth field is live, take the MIN of the two. The field knows the
        // true vertical depth to the terrain (which the screen-space reconstruction only
        // approximates, and gets wrong at grazing angles), while the screen-space value still
        // catches foam against boats, piers and anything else that is not terrain. The min keeps
        // both. With the flag clear this is the untouched VK-1604 expression.
        float shoreDepth = sceneVerticalDepth;
        if ((effectiveFlags & WATER_FLAG_SHORE_FIELD) != 0u)
            shoreDepth = min(fragShoreDepth, sceneVerticalDepth);

        // Skip shore effects for deep water or when terrain is far behind
        if (shoreDepth < pc.shoreFoamRange * 2.0) {
            // Wave-responsive modulation: foam line moves with wave displacement
            float waveModulation = fragOceanDispY * 0.5;
            float effectiveShoreRange = max(pc.shoreFoamRange + waveModulation, 0.5);

            // Base shore foam gradient
            float shoreFoam = (1.0 - smoothstep(0.0, effectiveShoreRange, shoreDepth)) * pc.shoreFoamIntensity;

            // Animated foam lines rolling toward shore
            float foamLine = smoothstep(0.4, 0.5, sin(shoreDepth * 6.0 - camera.u_Time * 1.5) * 0.5 + 0.5);
            shoreFoam = max(shoreFoam, foamLine * (1.0 - smoothstep(0.0, effectiveShoreRange * 0.7, shoreDepth)) * pc.shoreFoamIntensity);

            foam += shoreFoam;

            // Shore wave breaking: boost foam where waves are steep near shore
            if (pc.shoreBreakingStrength > 0.0) {
                float shoreProximity = 1.0 - smoothstep(0.0, effectiveShoreRange * 1.5, shoreDepth);
                float waveSteepness = 1.0 - dot(N, vec3(0.0, 1.0, 0.0));
                float breaking = shoreProximity * waveSteepness * 2.0 * pc.shoreBreakingStrength;

                // VK-1605: a band that the bottom has capped IS a breaking wave — that is exactly
                // what the depth limit models — so drive foam off how much was cut. The factor is
                // interpolated from the vertex stage (band 0) rather than recomputed, since the
                // shoaling math is far too heavy to run per pixel.
                if ((effectiveFlags & WATER_FLAG_SHOALING) != 0u)
                    breaking += (1.0 - fragShoalFactor) * pc.shoreBreakingStrength;

                foam += clamp(breaking, 0.0, 1.0);
            }
        }
    }

    foam = clamp(foam, 0.0, 1.0);
    vec3 foamColor = vec3(0.95, 0.97, 1.0);
    color = mix(color, foamColor, foam * 0.6);

    float alpha = mix(pc.shallowColor.a, 1.0, fresnel);

    outColor = vec4(color, alpha);
}
