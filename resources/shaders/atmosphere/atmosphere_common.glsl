// Shared atmosphere functions for all atmosphere shaders
// Bruneton/Hillaire physically-based atmosphere model

// --- Constants ---
const float PI = 3.14159265358979323846;

// --- Ray-Sphere Intersection ---
// Returns distances to near and far intersection points
// Returns false if no intersection
bool raySphereIntersect(vec3 rayOrigin, vec3 rayDir, vec3 sphereCenter, float sphereRadius,
                        out float t0, out float t1)
{
    vec3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float sqrtDisc = sqrt(disc);
    t0 = -b - sqrtDisc;
    t1 = -b + sqrtDisc;
    return true;
}

// Distance from ray origin to atmosphere boundary along rayDir
float distToAtmosphereBoundary(float planetRadius, float atmosphereRadius,
                                float altitude, float cosZenith)
{
    float r = planetRadius + altitude;
    float rSq = r * r;
    float atmoRSq = atmosphereRadius * atmosphereRadius;
    float rCosZ = r * cosZenith;
    return -rCosZ + sqrt(max(rCosZ * rCosZ - rSq + atmoRSq, 0.0));
}

// --- Density Functions ---
float rayleighDensity(float altitude, float densityExpScale)
{
    return exp(densityExpScale * altitude);
}

float mieDensity(float altitude, float densityExpScale)
{
    return exp(densityExpScale * altitude);
}

float ozoneDensity(float altitude, float centerAlt, float width)
{
    return max(0.0, 1.0 - abs(altitude - centerAlt) / width);
}

// --- Phase Functions ---
float rayleighPhase(float cosTheta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

float miePhase(float cosTheta, float g)
{
    float gSq = g * g;
    float num = (1.0 - gSq);
    float denom = 4.0 * PI * pow(1.0 + gSq - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

// --- Transmittance LUT Mapping ---
// Maps (altitude, cosZenith) to UV in transmittance LUT
vec2 transmittanceLUTParamsToUV(float planetRadius, float atmosphereRadius,
                                 float altitude, float cosZenith)
{
    float H = sqrt(max(atmosphereRadius * atmosphereRadius - planetRadius * planetRadius, 0.0));
    float rho = sqrt(max((planetRadius + altitude) * (planetRadius + altitude) - planetRadius * planetRadius, 0.0));

    float discriminant = (planetRadius + altitude) * (planetRadius + altitude) * (cosZenith * cosZenith - 1.0)
                         + atmosphereRadius * atmosphereRadius;
    float d = max(sqrt(max(discriminant, 0.0)) - (planetRadius + altitude) * cosZenith, 0.0);

    float dMin = atmosphereRadius - planetRadius - altitude;
    float dMax = rho + H;
    float mu = (d - dMin) / max(dMax - dMin, 0.0001);

    float r = rho / max(H, 0.0001);

    return vec2(mu, r);
}

// Inverse: UV to (altitude, cosZenith)
void transmittanceLUTUVToParams(float planetRadius, float atmosphereRadius,
                                 vec2 uv, out float altitude, out float cosZenith)
{
    float H = sqrt(max(atmosphereRadius * atmosphereRadius - planetRadius * planetRadius, 0.0));
    float rho = uv.y * H;
    float r = sqrt(rho * rho + planetRadius * planetRadius);
    altitude = r - planetRadius;

    float dMin = atmosphereRadius - r;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);

    cosZenith = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    cosZenith = clamp(cosZenith, -1.0, 1.0);
}

// --- Sky-View LUT Mapping ---
vec2 directionToSkyViewUV(vec3 viewDir, vec3 up, float altitude, float planetRadius, float atmosphereRadius)
{
    float cosZenith = dot(viewDir, up);

    // Horizon angle
    float r = planetRadius + altitude;
    float cosHorizon = -sqrt(max(1.0 - (planetRadius * planetRadius) / (r * r), 0.0));

    float v;
    if (cosZenith > cosHorizon)
    {
        // Above horizon
        v = 0.5 + 0.5 * sqrt(max((cosZenith - cosHorizon) / (1.0 - cosHorizon), 0.0));
    }
    else
    {
        // Below horizon
        v = 0.5 - 0.5 * sqrt(max((cosHorizon - cosZenith) / (1.0 + cosHorizon), 0.0));
    }

    // Azimuth: project viewDir onto horizontal plane, measure angle from sun direction projected
    vec3 forward = normalize(viewDir - up * cosZenith);
    float u = atan(forward.x, forward.z) / (2.0 * PI) + 0.5;

    return vec2(u, v);
}

vec3 skyViewUVToDirection(vec2 uv, vec3 up, vec3 sunDir, float altitude, float planetRadius)
{
    float r = planetRadius + altitude;
    float cosHorizon = -sqrt(max(1.0 - (planetRadius * planetRadius) / (r * r), 0.0));

    float v = uv.y;
    float cosZenith;
    if (v > 0.5)
    {
        float t = (v - 0.5) * 2.0;
        cosZenith = cosHorizon + t * t * (1.0 - cosHorizon);
    }
    else
    {
        float t = (0.5 - v) * 2.0;
        cosZenith = cosHorizon - t * t * (1.0 + cosHorizon);
    }

    float sinZenith = sqrt(max(1.0 - cosZenith * cosZenith, 0.0));

    float azimuth = (uv.x - 0.5) * 2.0 * PI;

    // Build local frame: up, right, forward
    vec3 right = normalize(cross(vec3(0.0, 0.0, 1.0), up));
    if (length(right) < 0.001)
        right = normalize(cross(vec3(1.0, 0.0, 0.0), up));
    vec3 forward = cross(up, right);

    return normalize(up * cosZenith + (right * sin(azimuth) + forward * cos(azimuth)) * sinZenith);
}

// --- Optical Depth Integration (used by transmittance LUT compute) ---
vec3 computeOpticalDepth(float planetRadius, float atmosphereRadius,
                          float altitude, float cosZenith,
                          vec3 rayleighScat, float rayleighExpScale,
                          float mieScat, float mieAbs, float mieExpScale,
                          vec3 ozoneAbs, float ozoneCenterAlt, float ozoneW,
                          int numSamples)
{
    float dist = distToAtmosphereBoundary(planetRadius, atmosphereRadius, altitude, cosZenith);
    float ds = dist / float(numSamples);

    vec3 opticalDepth = vec3(0.0);
    float r = planetRadius + altitude;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = (float(i) + 0.5) * ds;
        float sampleR = sqrt(r * r + 2.0 * r * cosZenith * t + t * t);
        float sampleAlt = sampleR - planetRadius;

        if (sampleAlt < 0.0) break;

        float densityR = rayleighDensity(sampleAlt, rayleighExpScale);
        float densityM = mieDensity(sampleAlt, mieExpScale);
        float densityO = ozoneDensity(sampleAlt, ozoneCenterAlt, ozoneW);

        vec3 extinction = rayleighScat * densityR
                        + (mieScat + mieAbs) * densityM
                        + ozoneAbs * densityO;

        opticalDepth += extinction * ds;
    }

    return opticalDepth;
}

// Sample transmittance from precomputed LUT
vec3 sampleTransmittanceLUT(sampler2D transmittanceLUT,
                              float planetRadius, float atmosphereRadius,
                              float altitude, float cosZenith)
{
    vec2 uv = transmittanceLUTParamsToUV(planetRadius, atmosphereRadius, altitude, cosZenith);
    return texture(transmittanceLUT, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
}
