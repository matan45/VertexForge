// Cloud common utilities - noise functions, density sampling, height gradients
// Shared between cloud_noise_gen.glsl and cloud_raymarch.glsl

// ──────────────────────────────────────────────────────────────
// Hash / noise primitives
// ──────────────────────────────────────────────────────────────

float hash3D(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 hash3Dvec3(vec3 p)
{
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

// ──────────────────────────────────────────────────────────────
// Worley noise  (returns distance to closest feature point)
// ──────────────────────────────────────────────────────────────

float worleyNoise3D(vec3 p)
{
    vec3 id = floor(p);
    vec3 fd = fract(p);
    float minDist = 1.0;

    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    for (int z = -1; z <= 1; ++z)
    {
        vec3 offset = vec3(x, y, z);
        vec3 featurePoint = hash3Dvec3(id + offset);
        vec3 diff = offset + featurePoint - fd;
        float dist = dot(diff, diff);
        minDist = min(minDist, dist);
    }
    return sqrt(minDist);
}

// ──────────────────────────────────────────────────────────────
// Perlin noise (3D, smooth gradient noise)
// ──────────────────────────────────────────────────────────────

float perlinNoise3D(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash3D(i + vec3(0,0,0));
    float n100 = hash3D(i + vec3(1,0,0));
    float n010 = hash3D(i + vec3(0,1,0));
    float n110 = hash3D(i + vec3(1,1,0));
    float n001 = hash3D(i + vec3(0,0,1));
    float n101 = hash3D(i + vec3(1,0,1));
    float n011 = hash3D(i + vec3(0,1,1));
    float n111 = hash3D(i + vec3(1,1,1));

    float x0 = mix(n000, n100, u.x);
    float x1 = mix(n010, n110, u.x);
    float x2 = mix(n001, n101, u.x);
    float x3 = mix(n011, n111, u.x);

    float y0 = mix(x0, x1, u.y);
    float y1 = mix(x2, x3, u.y);

    return mix(y0, y1, u.z);
}

// Perlin-Worley hybrid (used for shape noise)
float perlinWorley(vec3 p, float freq)
{
    float perlin = perlinNoise3D(p * freq);
    float worley = worleyNoise3D(p * freq);
    return mix(perlin, 1.0 - worley, 0.5);
}

// ──────────────────────────────────────────────────────────────
// FBM (fractal Brownian motion) for Worley
// ──────────────────────────────────────────────────────────────

float worleyFBM(vec3 p, int octaves)
{
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += (1.0 - worleyNoise3D(p * freq)) * amp;
        freq *= 2.0;
        amp *= 0.5;
    }
    return sum;
}

// ──────────────────────────────────────────────────────────────
// Height gradient for cloud density shaping
// ──────────────────────────────────────────────────────────────

// Returns height fraction [0,1] within cloud layer
float getHeightFraction(float altitude, float cloudMinAlt, float cloudMaxAlt)
{
    return clamp((altitude - cloudMinAlt) / (cloudMaxAlt - cloudMinAlt), 0.0, 1.0);
}

// Height-dependent density gradient based on cloud type (stratus=0, cumulus=1)
float heightGradient(float heightFrac, float cloudType)
{
    // Stratus: thin flat layer
    float stratus = smoothstep(0.0, 0.1, heightFrac) * smoothstep(0.4, 0.2, heightFrac);

    // Cumulus: tall rounded shape
    float cumulus = smoothstep(0.0, 0.15, heightFrac) * smoothstep(1.0, 0.6, heightFrac);

    return mix(stratus, cumulus, cloudType);
}

// ──────────────────────────────────────────────────────────────
// Ray-sphere intersection
// ──────────────────────────────────────────────────────────────

// Returns (near, far) intersection distances. Returns (-1,-1) if no intersection.
vec2 raySphereIntersect(vec3 rayOrigin, vec3 rayDir, vec3 sphereCenter, float sphereRadius)
{
    vec3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - c;

    if (discriminant < 0.0)
        return vec2(-1.0);

    float sqrtD = sqrt(discriminant);
    return vec2(-b - sqrtD, -b + sqrtD);
}

// ──────────────────────────────────────────────────────────────
// Phase functions for cloud lighting
// ──────────────────────────────────────────────────────────────

float henyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

float dualLobePhase(float cosTheta, float g1, float g2, float blend)
{
    return mix(henyeyGreenstein(cosTheta, g1), henyeyGreenstein(cosTheta, g2), blend);
}

// Beer-Lambert extinction
float beerLambert(float density, float absorption)
{
    return exp(-density * absorption);
}

// Powder effect (darkens cloud base, brightens edges)
float powderEffect(float density, float cosTheta)
{
    float powder = 1.0 - exp(-density * 2.0);
    return mix(1.0, powder, smoothstep(0.5, -0.5, cosTheta));
}

// ──────────────────────────────────────────────────────────────
// Weather map sampling helpers
// ──────────────────────────────────────────────────────────────

// Remap value from one range to another
float remap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
    return newMin + (value - oldMin) / (oldMax - oldMin) * (newMax - newMin);
}
