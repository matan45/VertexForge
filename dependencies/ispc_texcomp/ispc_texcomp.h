#pragma once

#include <cstdint>

// ISPCTextureCompressor - BC7/BC6H block compression via ISPC kernels
// Precompiled SIMD-optimized texture compression

struct rgba_surface
{
    uint8_t* ptr;
    int32_t width;
    int32_t height;
    int32_t stride; // bytes per row
};

struct bc7_enc_settings
{
    bool mode_selection[4];
    int refineIterations[8];
    bool skip_mode2;
    int fastSkipTreshold_mode1;
    int fastSkipTreshold_mode3;
    int fastSkipTreshold_mode7;
    int mode45_channel0;
    int refineIterations_channel;
    int channels;
};

struct bc6h_enc_settings
{
    bool slow_mode;
    bool fast_mode;
    int refineIterations_1p;
    int refineIterations_2p;
    int fastSkipTreshold;
};

// BC7 profile presets
extern "C" void GetProfile_ultrafast(bc7_enc_settings* settings);
extern "C" void GetProfile_veryfast(bc7_enc_settings* settings);
extern "C" void GetProfile_fast(bc7_enc_settings* settings);
extern "C" void GetProfile_basic(bc7_enc_settings* settings);
extern "C" void GetProfile_slow(bc7_enc_settings* settings);
extern "C" void GetProfile_alpha_ultrafast(bc7_enc_settings* settings);
extern "C" void GetProfile_alpha_veryfast(bc7_enc_settings* settings);
extern "C" void GetProfile_alpha_fast(bc7_enc_settings* settings);
extern "C" void GetProfile_alpha_basic(bc7_enc_settings* settings);
extern "C" void GetProfile_alpha_slow(bc7_enc_settings* settings);

// BC6H profile presets
extern "C" void GetProfile_bc6h_fast(bc6h_enc_settings* settings);
extern "C" void GetProfile_bc6h_basic(bc6h_enc_settings* settings);
extern "C" void GetProfile_bc6h_slow(bc6h_enc_settings* settings);
extern "C" void GetProfile_bc6h_veryslow(bc6h_enc_settings* settings);

// Compress RGBA8 surface to BC7 (16 bytes per 4x4 block)
extern "C" void CompressBlocksBC7(const rgba_surface* src, uint8_t* dst, const bc7_enc_settings* settings);

// Compress RGBA16F/32F surface to BC6H (16 bytes per 4x4 block)
// src->ptr points to float16 data (4 channels x 2 bytes = 8 bytes per pixel)
extern "C" void CompressBlocksBC6H(const rgba_surface* src, uint8_t* dst, const bc6h_enc_settings* settings);
