// BC7/BC6H block compression encoder
// Provides three quality tiers:
//   Fast:     min/max endpoints, linear index mapping (no per-pixel search)
//   Balanced: min/max endpoints, brute-force best index per pixel
//   Quality:  PCA endpoint selection + brute-force indices + endpoint refinement

#include "ispc_texcomp.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdint>

// ============================================================================
// Half-float helpers
// ============================================================================

static float halfToFloat(uint16_t h)
{
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;

    uint32_t f;
    if (exponent == 0)
    {
        if (mantissa == 0)
            f = sign << 31;
        else
        {
            exponent = 1;
            while (!(mantissa & 0x400)) { mantissa <<= 1; exponent--; }
            mantissa &= 0x3FF;
            f = (sign << 31) | ((exponent + 127 - 15) << 23) | (mantissa << 13);
        }
    }
    else if (exponent == 31)
        f = (sign << 31) | 0x7F800000 | (mantissa << 13);
    else
        f = (sign << 31) | ((exponent + 127 - 15) << 23) | (mantissa << 13);

    float result;
    memcpy(&result, &f, 4);
    return result;
}

static uint16_t floatToHalf(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, 4);
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exponent = ((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFF;
    if (exponent <= 0)
    {
        if (exponent < -10) return static_cast<uint16_t>(sign);
        mantissa |= 0x800000;
        mantissa >>= static_cast<uint32_t>(1 - exponent);
        return static_cast<uint16_t>(sign | (mantissa >> 13));
    }
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00);
    return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
}

// ============================================================================
// Quality levels encoded in settings
// ============================================================================

enum QualityLevel { QUALITY_FAST = 0, QUALITY_BALANCED = 1, QUALITY_HIGH = 2 };

// BC7 profiles - encode quality in refineIterations[0]
void GetProfile_ultrafast(bc7_enc_settings* s) { memset(s, 0, sizeof(*s)); s->channels = 4; s->refineIterations[0] = QUALITY_FAST; }
void GetProfile_veryfast(bc7_enc_settings* s)  { GetProfile_ultrafast(s); }
void GetProfile_fast(bc7_enc_settings* s)      { memset(s, 0, sizeof(*s)); s->channels = 4; s->refineIterations[0] = QUALITY_FAST; }
void GetProfile_basic(bc7_enc_settings* s)     { memset(s, 0, sizeof(*s)); s->channels = 4; s->refineIterations[0] = QUALITY_BALANCED; }
void GetProfile_slow(bc7_enc_settings* s)      { memset(s, 0, sizeof(*s)); s->channels = 4; s->refineIterations[0] = QUALITY_HIGH; }

void GetProfile_alpha_ultrafast(bc7_enc_settings* s) { GetProfile_ultrafast(s); }
void GetProfile_alpha_veryfast(bc7_enc_settings* s)  { GetProfile_veryfast(s); }
void GetProfile_alpha_fast(bc7_enc_settings* s)      { GetProfile_fast(s); }
void GetProfile_alpha_basic(bc7_enc_settings* s)     { GetProfile_basic(s); }
void GetProfile_alpha_slow(bc7_enc_settings* s)      { GetProfile_slow(s); }

// BC6H profiles - encode quality in refineIterations_1p
void GetProfile_bc6h_fast(bc6h_enc_settings* s)     { memset(s, 0, sizeof(*s)); s->fast_mode = true;  s->refineIterations_1p = QUALITY_FAST; }
void GetProfile_bc6h_basic(bc6h_enc_settings* s)    { memset(s, 0, sizeof(*s)); s->refineIterations_1p = QUALITY_BALANCED; }
void GetProfile_bc6h_slow(bc6h_enc_settings* s)     { memset(s, 0, sizeof(*s)); s->slow_mode = true;  s->refineIterations_1p = QUALITY_HIGH; }
void GetProfile_bc6h_veryslow(bc6h_enc_settings* s) { GetProfile_bc6h_slow(s); }

// ============================================================================
// Bit-packing helper for 128-bit blocks
// ============================================================================

static void setBits128(uint64_t& lo, uint64_t& hi, int startBit, int numBits, uint64_t value)
{
    uint64_t mask = (numBits >= 64) ? ~0ULL : ((1ULL << numBits) - 1);
    value &= mask;
    if (startBit < 64)
    {
        lo |= value << startBit;
        if (startBit + numBits > 64)
            hi |= value >> (64 - startBit);
    }
    else
    {
        hi |= value << (startBit - 64);
    }
}

// ============================================================================
// BC7 Mode 6 interpolation weights
// ============================================================================

static const int bc7_weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

// ============================================================================
// PCA: find principal axis of a set of RGBA8 pixels (for better endpoint selection)
// ============================================================================

static void computePCA_RGBA(const uint8_t block[16][4], float axis[4])
{
    // Compute mean
    float mean[4] = {};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c)
            mean[c] += block[i][c];
    for (int c = 0; c < 4; ++c)
        mean[c] /= 16.0f;

    // Compute covariance matrix (4x4, symmetric)
    float cov[4][4] = {};
    for (int i = 0; i < 16; ++i)
    {
        float d[4];
        for (int c = 0; c < 4; ++c)
            d[c] = block[i][c] - mean[c];
        for (int a = 0; a < 4; ++a)
            for (int b = a; b < 4; ++b)
                cov[a][b] += d[a] * d[b];
    }
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            cov[b][a] = cov[a][b];

    // Power iteration to find dominant eigenvector
    float v[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (int iter = 0; iter < 8; ++iter)
    {
        float nv[4] = {};
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
                nv[a] += cov[a][b] * v[b];
        float len = 0;
        for (int c = 0; c < 4; ++c)
            len += nv[c] * nv[c];
        len = sqrtf(len);
        if (len < 1e-10f) { v[0] = 1; v[1] = v[2] = v[3] = 0; break; }
        for (int c = 0; c < 4; ++c)
            v[c] = nv[c] / len;
    }
    for (int c = 0; c < 4; ++c)
        axis[c] = v[c];
}

// Project pixels onto axis, find min/max projected endpoints
static void projectEndpoints_RGBA(const uint8_t block[16][4], const float axis[4],
                                   uint8_t ep0[4], uint8_t ep1[4])
{
    float mean[4] = {};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c)
            mean[c] += block[i][c];
    for (int c = 0; c < 4; ++c)
        mean[c] /= 16.0f;

    float minProj = 1e30f, maxProj = -1e30f;
    for (int i = 0; i < 16; ++i)
    {
        float proj = 0;
        for (int c = 0; c < 4; ++c)
            proj += (block[i][c] - mean[c]) * axis[c];
        if (proj < minProj) minProj = proj;
        if (proj > maxProj) maxProj = proj;
    }

    for (int c = 0; c < 4; ++c)
    {
        float v0 = mean[c] + axis[c] * minProj;
        float v1 = mean[c] + axis[c] * maxProj;
        ep0[c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v0 + 0.5f)));
        ep1[c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v1 + 0.5f)));
    }
}

// Endpoint refinement: given indices, recompute optimal endpoints via least-squares
static void refineEndpoints_RGBA(const uint8_t block[16][4], const uint8_t indices[16],
                                  uint8_t ep0[4], uint8_t ep1[4], uint8_t& p0, uint8_t& p1)
{
    // For each channel, solve: color[i] ≈ e0*(64-w)/64 + e1*w/64
    // Least squares: minimize sum( (color_i - e0*(64-w_i)/64 - e1*w_i/64)^2 )
    for (int c = 0; c < 4; ++c)
    {
        float sumA00 = 0, sumA01 = 0, sumA11 = 0, sumB0 = 0, sumB1 = 0;
        for (int i = 0; i < 16; ++i)
        {
            float w1 = bc7_weights4[indices[i]] / 64.0f;
            float w0 = 1.0f - w1;
            sumA00 += w0 * w0;
            sumA01 += w0 * w1;
            sumA11 += w1 * w1;
            sumB0 += w0 * block[i][c];
            sumB1 += w1 * block[i][c];
        }
        float det = sumA00 * sumA11 - sumA01 * sumA01;
        if (fabsf(det) > 1e-6f)
        {
            float e0f = (sumA11 * sumB0 - sumA01 * sumB1) / det;
            float e1f = (sumA00 * sumB1 - sumA01 * sumB0) / det;
            ep0[c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, e0f + 0.5f))) >> 1;
            ep1[c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, e1f + 0.5f))) >> 1;
        }
    }
    p0 = 0;
    p1 = 0;
}

// ============================================================================
// BC7 Mode 6 encoder
// ============================================================================

void CompressBlocksBC7(const rgba_surface* src, uint8_t* dst, const bc7_enc_settings* settings)
{
    int blocksX = (src->width + 3) / 4;
    int blocksY = (src->height + 3) / 4;
    int quality = settings ? settings->refineIterations[0] : QUALITY_BALANCED;

    for (int by = 0; by < blocksY; ++by)
    {
        for (int bx = 0; bx < blocksX; ++bx)
        {
            // Extract 4x4 block
            uint8_t block[16][4];
            for (int py = 0; py < 4; ++py)
            {
                int srcY = std::min(by * 4 + py, src->height - 1);
                for (int px = 0; px < 4; ++px)
                {
                    int srcX = std::min(bx * 4 + px, src->width - 1);
                    const uint8_t* p = src->ptr + srcY * src->stride + srcX * 4;
                    memcpy(block[py * 4 + px], p, 4);
                }
            }

            // --- Endpoint selection ---
            uint8_t ep0[4], ep1[4];
            uint8_t p0 = 0, p1 = 0;

            if (quality >= QUALITY_HIGH)
            {
                // PCA-based endpoints
                float axis[4];
                computePCA_RGBA(block, axis);
                projectEndpoints_RGBA(block, axis, ep0, ep1);
                for (int c = 0; c < 4; ++c) { ep0[c] >>= 1; ep1[c] >>= 1; }
            }
            else
            {
                // Min/max endpoints
                uint8_t minC[4] = {255,255,255,255}, maxC[4] = {0,0,0,0};
                for (int i = 0; i < 16; ++i)
                    for (int c = 0; c < 4; ++c)
                    {
                        if (block[i][c] < minC[c]) minC[c] = block[i][c];
                        if (block[i][c] > maxC[c]) maxC[c] = block[i][c];
                    }
                for (int c = 0; c < 4; ++c) { ep0[c] = minC[c] >> 1; ep1[c] = maxC[c] >> 1; }
                p0 = minC[0] & 1; p1 = maxC[0] & 1;
            }

            // --- Index computation ---
            uint8_t indices[16];
            int e0[4], e1[4];
            for (int c = 0; c < 4; ++c) { e0[c] = (ep0[c] << 1) | p0; e1[c] = (ep1[c] << 1) | p1; }

            if (quality >= QUALITY_BALANCED)
            {
                // Brute-force best index per pixel
                for (int i = 0; i < 16; ++i)
                {
                    int bestIdx = 0, bestErr = INT32_MAX;
                    for (int idx = 0; idx < 16; ++idx)
                    {
                        int w = bc7_weights4[idx];
                        int err = 0;
                        for (int c = 0; c < 4; ++c)
                        {
                            int interp = (e0[c] * (64 - w) + e1[c] * w + 32) >> 6;
                            int d = block[i][c] - interp;
                            err += d * d;
                        }
                        if (err < bestErr) { bestErr = err; bestIdx = idx; }
                    }
                    indices[i] = static_cast<uint8_t>(bestIdx);
                }
            }
            else
            {
                // Fast: linear interpolation (no search)
                for (int i = 0; i < 16; ++i)
                {
                    // Project pixel onto endpoint line, compute approximate index
                    int dot = 0, lenSq = 0;
                    for (int c = 0; c < 4; ++c)
                    {
                        int d = e1[c] - e0[c];
                        dot += (block[i][c] - e0[c]) * d;
                        lenSq += d * d;
                    }
                    int idx = (lenSq > 0) ? (dot * 15 + lenSq / 2) / lenSq : 0;
                    indices[i] = static_cast<uint8_t>(std::max(0, std::min(15, idx)));
                }
            }

            // --- Endpoint refinement (Quality mode) ---
            if (quality >= QUALITY_HIGH)
            {
                // Refine endpoints from indices, then recompute indices
                for (int refinePass = 0; refinePass < 2; ++refinePass)
                {
                    refineEndpoints_RGBA(block, indices, ep0, ep1, p0, p1);
                    for (int c = 0; c < 4; ++c) { e0[c] = (ep0[c] << 1) | p0; e1[c] = (ep1[c] << 1) | p1; }
                    for (int i = 0; i < 16; ++i)
                    {
                        int bestIdx = 0, bestErr = INT32_MAX;
                        for (int idx = 0; idx < 16; ++idx)
                        {
                            int w = bc7_weights4[idx];
                            int err = 0;
                            for (int c = 0; c < 4; ++c)
                            {
                                int interp = (e0[c] * (64 - w) + e1[c] * w + 32) >> 6;
                                int d = block[i][c] - interp;
                                err += d * d;
                            }
                            if (err < bestErr) { bestErr = err; bestIdx = idx; }
                        }
                        indices[i] = static_cast<uint8_t>(bestIdx);
                    }
                }
            }

            // --- Anchor constraint: index 0 MSB must be 0 ---
            if (indices[0] & 0x8)
            {
                std::swap(ep0[0], ep1[0]); std::swap(ep0[1], ep1[1]);
                std::swap(ep0[2], ep1[2]); std::swap(ep0[3], ep1[3]);
                std::swap(p0, p1);
                for (int i = 0; i < 16; ++i) indices[i] = 15 - indices[i];
            }

            // --- Pack 128-bit block ---
            uint64_t lo = 0, hi = 0;
            setBits128(lo, hi, 0, 7, 0x40);
            setBits128(lo, hi, 7,  7, ep0[0]);  setBits128(lo, hi, 14, 7, ep1[0]);
            setBits128(lo, hi, 21, 7, ep0[1]);  setBits128(lo, hi, 28, 7, ep1[1]);
            setBits128(lo, hi, 35, 7, ep0[2]);  setBits128(lo, hi, 42, 7, ep1[2]);
            setBits128(lo, hi, 49, 7, ep0[3]);  setBits128(lo, hi, 56, 7, ep1[3]);
            setBits128(lo, hi, 63, 1, p0);
            setBits128(lo, hi, 64, 1, p1);
            setBits128(lo, hi, 65, 3, indices[0]);
            for (int i = 1; i < 16; ++i)
                setBits128(lo, hi, 65 + 3 + (i - 1) * 4, 4, indices[i]);

            uint8_t* out = dst + (by * blocksX + bx) * 16;
            memcpy(out, &lo, 8);
            memcpy(out + 8, &hi, 8);
        }
    }
}

// ============================================================================
// PCA for RGB float block (BC6H)
// ============================================================================

static void computePCA_RGB(const float block[16][3], float axis[3])
{
    float mean[3] = {};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c)
            mean[c] += block[i][c];
    for (int c = 0; c < 3; ++c)
        mean[c] /= 16.0f;

    float cov[3][3] = {};
    for (int i = 0; i < 16; ++i)
    {
        float d[3];
        for (int c = 0; c < 3; ++c) d[c] = block[i][c] - mean[c];
        for (int a = 0; a < 3; ++a)
            for (int b = a; b < 3; ++b)
                cov[a][b] += d[a] * d[b];
    }
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b)
            cov[b][a] = cov[a][b];

    float v[3] = {1.0f, 1.0f, 1.0f};
    for (int iter = 0; iter < 8; ++iter)
    {
        float nv[3] = {};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                nv[a] += cov[a][b] * v[b];
        float len = sqrtf(nv[0]*nv[0] + nv[1]*nv[1] + nv[2]*nv[2]);
        if (len < 1e-10f) { v[0] = 1; v[1] = v[2] = 0; break; }
        for (int c = 0; c < 3; ++c) v[c] = nv[c] / len;
    }
    for (int c = 0; c < 3; ++c) axis[c] = v[c];
}

// ============================================================================
// BC6H helpers
// ============================================================================

// BC6H unsigned endpoint quantization.
// The GPU decodes as:
//   1. unq = ((ep << 15) + 0x4000) >> 9          (special: ep==0 -> 0, ep==1023 -> 0xFFFF)
//   2. final_half_bits = (unq * 31) >> 6          (this IS the half-float bit pattern)
//
// To encode a float -> 10-bit endpoint, we invert both steps:
//   target_half = floatToHalf(value)
//   target_unq  = (target_half * 64 + 15) / 31   (inverse of step 2, rounded)
//   ep          = target_unq / 64                 (inverse of step 1, simplified)

static uint16_t floatToBC6HEndpoint(float value)
{
    if (value <= 0.0f) return 0;
    uint16_t h = floatToHalf(value);
    uint32_t target_unq = ((uint32_t)h * 64 + 15) / 31;
    uint32_t ep = target_unq / 64;
    return static_cast<uint16_t>(std::min(ep, 1023u));
}

// Decode a 10-bit endpoint to float, matching GPU decode exactly
static float bc6hEndpointToFloat(uint16_t ep10)
{
    uint32_t unq;
    if (ep10 == 0)
        unq = 0;
    else if (ep10 == 1023)
        unq = 0xFFFF;
    else
        unq = (((uint32_t)ep10 << 15) + 0x4000) >> 9;

    // Final unquantization: result is the half-float bit pattern
    uint32_t final_half = (unq * 31) >> 6;
    return halfToFloat(static_cast<uint16_t>(std::min(final_half, 0xFFFFu)));
}

// Simulate GPU interpolation for a given weight, returning float value
// This matches the GPU decode exactly: interpolate in unq space, then finalize
static float bc6hInterpolate(uint32_t unq0, uint32_t unq1, int weight)
{
    uint32_t interp = (unq0 * (64 - weight) + unq1 * weight + 32) >> 6;
    uint32_t final_half = (interp * 31) >> 6;
    return halfToFloat(static_cast<uint16_t>(std::min(final_half, 0xFFFFu)));
}

// Unquantize a 10-bit endpoint to the intermediate 16-bit value
static uint32_t bc6hUnquantize(uint16_t ep10)
{
    if (ep10 == 0) return 0;
    if (ep10 == 1023) return 0xFFFF;
    return (((uint32_t)ep10 << 15) + 0x4000) >> 9;
}

// ============================================================================
// BC6H Mode 11 encoder
// ============================================================================

void CompressBlocksBC6H(const rgba_surface* src, uint8_t* dst, const bc6h_enc_settings* settings)
{
    int blocksX = (src->width + 3) / 4;
    int blocksY = (src->height + 3) / 4;
    int quality = settings ? settings->refineIterations_1p : QUALITY_BALANCED;

    for (int by = 0; by < blocksY; ++by)
    {
        for (int bx = 0; bx < blocksX; ++bx)
        {
            // Extract 4x4 block -> float RGB
            float block[16][3];
            for (int py = 0; py < 4; ++py)
            {
                int srcY = std::min(by * 4 + py, src->height - 1);
                for (int px = 0; px < 4; ++px)
                {
                    int srcX = std::min(bx * 4 + px, src->width - 1);
                    const uint16_t* p = reinterpret_cast<const uint16_t*>(
                        src->ptr + srcY * src->stride) + srcX * 4;
                    block[py * 4 + px][0] = halfToFloat(p[0]);
                    block[py * 4 + px][1] = halfToFloat(p[1]);
                    block[py * 4 + px][2] = halfToFloat(p[2]);
                }
            }

            // --- Endpoint selection ---
            float ep0f[3], ep1f[3];

            if (quality >= QUALITY_HIGH)
            {
                // PCA-based
                float axis[3];
                computePCA_RGB(block, axis);
                float mean[3] = {};
                for (int i = 0; i < 16; ++i)
                    for (int c = 0; c < 3; ++c)
                        mean[c] += block[i][c];
                for (int c = 0; c < 3; ++c) mean[c] /= 16.0f;

                float minProj = 1e30f, maxProj = -1e30f;
                for (int i = 0; i < 16; ++i)
                {
                    float proj = 0;
                    for (int c = 0; c < 3; ++c)
                        proj += (block[i][c] - mean[c]) * axis[c];
                    minProj = std::min(minProj, proj);
                    maxProj = std::max(maxProj, proj);
                }
                for (int c = 0; c < 3; ++c)
                {
                    ep0f[c] = std::max(0.0f, mean[c] + axis[c] * minProj);
                    ep1f[c] = std::max(0.0f, mean[c] + axis[c] * maxProj);
                }
            }
            else
            {
                // Min/max
                for (int c = 0; c < 3; ++c) { ep0f[c] = block[0][c]; ep1f[c] = block[0][c]; }
                for (int i = 1; i < 16; ++i)
                    for (int c = 0; c < 3; ++c)
                    {
                        ep0f[c] = std::min(ep0f[c], block[i][c]);
                        ep1f[c] = std::max(ep1f[c], block[i][c]);
                    }
            }

            // Quantize endpoints
            uint16_t ep0[3], ep1[3];
            uint32_t unq0[3], unq1[3];
            for (int c = 0; c < 3; ++c)
            {
                ep0[c] = floatToBC6HEndpoint(ep0f[c]);
                ep1[c] = floatToBC6HEndpoint(ep1f[c]);
                unq0[c] = bc6hUnquantize(ep0[c]);
                unq1[c] = bc6hUnquantize(ep1[c]);
            }

            // --- Index computation (GPU-accurate interpolation) ---
            uint8_t indices[16];

            if (quality >= QUALITY_BALANCED)
            {
                // Brute-force best index using exact GPU decode
                for (int i = 0; i < 16; ++i)
                {
                    int bestIdx = 0; float bestErr = 1e30f;
                    for (int idx = 0; idx < 16; ++idx)
                    {
                        int w = bc7_weights4[idx];
                        float err = 0;
                        for (int c = 0; c < 3; ++c)
                        {
                            float decoded = bc6hInterpolate(unq0[c], unq1[c], w);
                            float d = block[i][c] - decoded;
                            err += d * d;
                        }
                        if (err < bestErr) { bestErr = err; bestIdx = idx; }
                    }
                    indices[i] = static_cast<uint8_t>(bestIdx);
                }
            }
            else
            {
                // Fast: linear projection using decoded endpoint floats
                float e0dec[3], e1dec[3];
                for (int c = 0; c < 3; ++c)
                {
                    e0dec[c] = bc6hEndpointToFloat(ep0[c]);
                    e1dec[c] = bc6hEndpointToFloat(ep1[c]);
                }
                for (int i = 0; i < 16; ++i)
                {
                    float dot = 0, lenSq = 0;
                    for (int c = 0; c < 3; ++c)
                    {
                        float d = e1dec[c] - e0dec[c];
                        dot += (block[i][c] - e0dec[c]) * d;
                        lenSq += d * d;
                    }
                    int idx = (lenSq > 1e-10f) ? static_cast<int>(dot / lenSq * 15.0f + 0.5f) : 0;
                    indices[i] = static_cast<uint8_t>(std::max(0, std::min(15, idx)));
                }
            }

            // --- Endpoint refinement (Quality mode) ---
            if (quality >= QUALITY_HIGH)
            {
                for (int refinePass = 0; refinePass < 2; ++refinePass)
                {
                    // Least-squares endpoint refit from current indices
                    for (int c = 0; c < 3; ++c)
                    {
                        float sA00 = 0, sA01 = 0, sA11 = 0, sB0 = 0, sB1 = 0;
                        for (int i = 0; i < 16; ++i)
                        {
                            float w1 = bc7_weights4[indices[i]] / 64.0f;
                            float w0 = 1.0f - w1;
                            sA00 += w0 * w0; sA01 += w0 * w1; sA11 += w1 * w1;
                            sB0 += w0 * block[i][c]; sB1 += w1 * block[i][c];
                        }
                        float det = sA00 * sA11 - sA01 * sA01;
                        if (fabsf(det) > 1e-6f)
                        {
                            ep0f[c] = std::max(0.0f, (sA11 * sB0 - sA01 * sB1) / det);
                            ep1f[c] = std::max(0.0f, (sA00 * sB1 - sA01 * sB0) / det);
                        }
                    }
                    for (int c = 0; c < 3; ++c)
                    {
                        ep0[c] = floatToBC6HEndpoint(ep0f[c]);
                        ep1[c] = floatToBC6HEndpoint(ep1f[c]);
                        unq0[c] = bc6hUnquantize(ep0[c]);
                        unq1[c] = bc6hUnquantize(ep1[c]);
                    }
                    // Recompute indices with GPU-accurate interpolation
                    for (int i = 0; i < 16; ++i)
                    {
                        int bestIdx = 0; float bestErr = 1e30f;
                        for (int idx = 0; idx < 16; ++idx)
                        {
                            int w = bc7_weights4[idx];
                            float err = 0;
                            for (int c = 0; c < 3; ++c)
                            {
                                float decoded = bc6hInterpolate(unq0[c], unq1[c], w);
                                float d = block[i][c] - decoded;
                                err += d * d;
                            }
                            if (err < bestErr) { bestErr = err; bestIdx = idx; }
                        }
                        indices[i] = static_cast<uint8_t>(bestIdx);
                    }
                }
            }

            // --- Anchor constraint ---
            if (indices[0] & 0x8)
            {
                for (int c = 0; c < 3; ++c) std::swap(ep0[c], ep1[c]);
                for (int i = 0; i < 16; ++i) indices[i] = 15 - indices[i];
            }

            // --- Pack 128-bit block ---
            uint64_t lo = 0, hi = 0;
            // BC6H Mode 11 layout: mode, rw, gw, bw, rx, gx, bx (endpoint0 RGB then endpoint1 RGB)
            setBits128(lo, hi, 0,  5, 0x03);
            setBits128(lo, hi, 5,  10, ep0[0]); setBits128(lo, hi, 15, 10, ep0[1]); setBits128(lo, hi, 25, 10, ep0[2]);
            setBits128(lo, hi, 35, 10, ep1[0]); setBits128(lo, hi, 45, 10, ep1[1]); setBits128(lo, hi, 55, 10, ep1[2]);
            setBits128(lo, hi, 65, 3, indices[0]);
            for (int i = 1; i < 16; ++i)
                setBits128(lo, hi, 65 + 3 + (i - 1) * 4, 4, indices[i]);

            uint8_t* out = dst + (by * blocksX + bx) * 16;
            memcpy(out, &lo, 8);
            memcpy(out + 8, &hi, 8);
        }
    }
}
