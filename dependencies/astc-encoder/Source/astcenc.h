#pragma once

#include <cstdint>
#include <cstddef>

// astc-encoder: ARM ASTC texture compression library

enum astcenc_status
{
    ASTCENC_SUCCESS = 0,
    ASTCENC_ERR_OUT_OF_MEM,
    ASTCENC_ERR_BAD_PARAM,
    ASTCENC_ERR_BAD_BLOCK_SIZE,
    ASTCENC_ERR_BAD_PROFILE,
    ASTCENC_ERR_NOT_IMPLEMENTED,
    ASTCENC_ERR_BAD_FLAGS
};

enum astcenc_profile
{
    ASTCENC_PRF_LDR_SRGB = 0,
    ASTCENC_PRF_LDR,
    ASTCENC_PRF_HDR_RGB_LDR_A,
    ASTCENC_PRF_HDR
};

enum astcenc_swizzle_channel
{
    ASTCENC_SWZ_R = 0,
    ASTCENC_SWZ_G = 1,
    ASTCENC_SWZ_B = 2,
    ASTCENC_SWZ_A = 3,
    ASTCENC_SWZ_0 = 4,
    ASTCENC_SWZ_1 = 5
};

struct astcenc_swizzle
{
    astcenc_swizzle_channel r;
    astcenc_swizzle_channel g;
    astcenc_swizzle_channel b;
    astcenc_swizzle_channel a;
};

enum astcenc_type
{
    ASTCENC_TYPE_U8 = 0,
    ASTCENC_TYPE_F16 = 1,
    ASTCENC_TYPE_F32 = 2
};

#define ASTCENC_FLG_MAP_NORMAL        (1 << 0)
#define ASTCENC_FLG_MAP_MASK          (1 << 1)
#define ASTCENC_FLG_USE_ALPHA_WEIGHT  (1 << 2)
#define ASTCENC_FLG_USE_PERCEPTUAL    (1 << 3)
#define ASTCENC_FLG_DECOMPRESS_ONLY   (1 << 4)
#define ASTCENC_FLG_SELF_DECOMPRESS_ONLY (1 << 5)

#define ASTCENC_ALL_FLAGS ( \
    ASTCENC_FLG_MAP_NORMAL | \
    ASTCENC_FLG_MAP_MASK | \
    ASTCENC_FLG_USE_ALPHA_WEIGHT | \
    ASTCENC_FLG_USE_PERCEPTUAL | \
    ASTCENC_FLG_DECOMPRESS_ONLY | \
    ASTCENC_FLG_SELF_DECOMPRESS_ONLY)

struct astcenc_config
{
    astcenc_profile profile;
    unsigned int flags;
    unsigned int block_x;
    unsigned int block_y;
    unsigned int block_z;
    float cw_r_weight;
    float cw_g_weight;
    float cw_b_weight;
    float cw_a_weight;
    unsigned int a_scale_radius;
    float rgbm_m_scale;
    float tune_partition_count_limit;
    float tune_2partition_index_limit;
    float tune_3partition_index_limit;
    float tune_4partition_index_limit;
    float tune_2partitioning_candidate_limit;
    float tune_3partitioning_candidate_limit;
    float tune_4partitioning_candidate_limit;
    float tune_db_limit;
    float tune_mse_overshoot;
    float tune_2partition_early_out_limit_factor;
    float tune_3partition_early_out_limit_factor;
    float tune_2plane_early_out_limit_correlation;
    unsigned int tune_search_mode0_enable;
    float tune_refinement_limit;
};

struct astcenc_image
{
    unsigned int dim_x;
    unsigned int dim_y;
    unsigned int dim_z;
    astcenc_type data_type;
    void** data; // array of row pointers per z-slice
};

// Opaque context
struct astcenc_context;

// Quality presets
static const float ASTCENC_PRE_FASTEST  = 0.0f;
static const float ASTCENC_PRE_FAST     = 10.0f;
static const float ASTCENC_PRE_MEDIUM   = 60.0f;
static const float ASTCENC_PRE_THOROUGH = 98.0f;
static const float ASTCENC_PRE_EXHAUSTIVE = 100.0f;

// API functions
astcenc_status astcenc_config_init(
    astcenc_profile profile,
    unsigned int block_x,
    unsigned int block_y,
    unsigned int block_z,
    float quality,
    unsigned int flags,
    astcenc_config* config);

astcenc_status astcenc_context_alloc(
    const astcenc_config* config,
    unsigned int thread_count,
    astcenc_context** context);

astcenc_status astcenc_compress_image(
    astcenc_context* context,
    astcenc_image* image,
    const astcenc_swizzle* swizzle,
    uint8_t* data_out,
    size_t data_len,
    unsigned int thread_index);

astcenc_status astcenc_compress_reset(
    astcenc_context* context);

void astcenc_context_free(
    astcenc_context* context);

const char* astcenc_get_error_string(
    astcenc_status status);
