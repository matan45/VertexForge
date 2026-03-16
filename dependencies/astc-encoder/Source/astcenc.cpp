// astc-encoder: ARM ASTC texture compression library
// Reference implementation for VertexForge integration

#include "astcenc.h"
#include <cstring>
#include <cstdlib>

struct astcenc_context
{
    astcenc_config config;
};

astcenc_status astcenc_config_init(
    astcenc_profile profile,
    unsigned int block_x,
    unsigned int block_y,
    unsigned int block_z,
    float quality,
    unsigned int flags,
    astcenc_config* config)
{
    if (!config)
        return ASTCENC_ERR_BAD_PARAM;

    if (block_x < 4 || block_x > 12 || block_y < 4 || block_y > 12)
        return ASTCENC_ERR_BAD_BLOCK_SIZE;

    if (block_z != 1)
        return ASTCENC_ERR_BAD_BLOCK_SIZE;

    memset(config, 0, sizeof(astcenc_config));
    config->profile = profile;
    config->flags = flags;
    config->block_x = block_x;
    config->block_y = block_y;
    config->block_z = block_z;
    config->cw_r_weight = 1.0f;
    config->cw_g_weight = 1.0f;
    config->cw_b_weight = 1.0f;
    config->cw_a_weight = 1.0f;

    // Quality-dependent tuning
    if (quality <= ASTCENC_PRE_FAST)
    {
        config->tune_partition_count_limit = 4.0f;
        config->tune_db_limit = 80.0f;
    }
    else if (quality <= ASTCENC_PRE_MEDIUM)
    {
        config->tune_partition_count_limit = 25.0f;
        config->tune_db_limit = 95.0f;
    }
    else
    {
        config->tune_partition_count_limit = 100.0f;
        config->tune_db_limit = 999.0f;
    }

    return ASTCENC_SUCCESS;
}

astcenc_status astcenc_context_alloc(
    const astcenc_config* config,
    unsigned int /*thread_count*/,
    astcenc_context** context)
{
    if (!config || !context)
        return ASTCENC_ERR_BAD_PARAM;

    *context = new astcenc_context();
    (*context)->config = *config;
    return ASTCENC_SUCCESS;
}

astcenc_status astcenc_compress_image(
    astcenc_context* context,
    astcenc_image* image,
    const astcenc_swizzle* /*swizzle*/,
    uint8_t* data_out,
    size_t data_len,
    unsigned int /*thread_index*/)
{
    if (!context || !image || !data_out)
        return ASTCENC_ERR_BAD_PARAM;

    unsigned int block_x = context->config.block_x;
    unsigned int block_y = context->config.block_y;

    unsigned int blocks_x = (image->dim_x + block_x - 1) / block_x;
    unsigned int blocks_y = (image->dim_y + block_y - 1) / block_y;
    size_t total_blocks = static_cast<size_t>(blocks_x) * blocks_y;

    // Each ASTC block is 16 bytes
    if (data_len < total_blocks * 16)
        return ASTCENC_ERR_BAD_PARAM;

    // Reference implementation: encode each block
    for (unsigned int by = 0; by < blocks_y; ++by)
    {
        for (unsigned int bx = 0; bx < blocks_x; ++bx)
        {
            uint8_t* outBlock = data_out + (by * blocks_x + bx) * 16;

            // ASTC block header: encode block dimensions and a void-extent block
            // for the average color (simplified reference implementation)
            memset(outBlock, 0, 16);

            // Compute average color of the block
            float avgR = 0, avgG = 0, avgB = 0, avgA = 0;
            int pixelCount = 0;

            for (unsigned int py = 0; py < block_y; ++py)
            {
                unsigned int srcY = by * block_y + py;
                if (srcY >= image->dim_y) srcY = image->dim_y - 1;

                for (unsigned int px = 0; px < block_x; ++px)
                {
                    unsigned int srcX = bx * block_x + px;
                    if (srcX >= image->dim_x) srcX = image->dim_x - 1;

                    if (image->data_type == ASTCENC_TYPE_U8)
                    {
                        const uint8_t* row = static_cast<const uint8_t*>(image->data[srcY]);
                        avgR += row[srcX * 4 + 0] / 255.0f;
                        avgG += row[srcX * 4 + 1] / 255.0f;
                        avgB += row[srcX * 4 + 2] / 255.0f;
                        avgA += row[srcX * 4 + 3] / 255.0f;
                    }
                    ++pixelCount;
                }
            }

            if (pixelCount > 0)
            {
                avgR /= pixelCount;
                avgG /= pixelCount;
                avgB /= pixelCount;
                avgA /= pixelCount;
            }

            // Encode as void-extent block (constant color)
            // Void-extent block: bits [8:0] = 111111100 (0x1FC)
            outBlock[0] = 0xFC;
            outBlock[1] = 0x01;
            // Bits [2:12] all 1s for void extent
            outBlock[2] = 0xFF;
            outBlock[3] = 0xFF;
            outBlock[4] = 0xFF;
            outBlock[5] = 0xFF;
            outBlock[6] = 0xFF;
            outBlock[7] = 0xFF;

            // RGBA16 constant color values in bytes 8-15
            uint16_t r16 = static_cast<uint16_t>(avgR * 65535.0f + 0.5f);
            uint16_t g16 = static_cast<uint16_t>(avgG * 65535.0f + 0.5f);
            uint16_t b16 = static_cast<uint16_t>(avgB * 65535.0f + 0.5f);
            uint16_t a16 = static_cast<uint16_t>(avgA * 65535.0f + 0.5f);
            memcpy(outBlock + 8, &r16, 2);
            memcpy(outBlock + 10, &g16, 2);
            memcpy(outBlock + 12, &b16, 2);
            memcpy(outBlock + 14, &a16, 2);
        }
    }

    return ASTCENC_SUCCESS;
}

astcenc_status astcenc_compress_reset(astcenc_context* context)
{
    if (!context)
        return ASTCENC_ERR_BAD_PARAM;
    return ASTCENC_SUCCESS;
}

void astcenc_context_free(astcenc_context* context)
{
    delete context;
}

const char* astcenc_get_error_string(astcenc_status status)
{
    switch (status)
    {
        case ASTCENC_SUCCESS:           return "Success";
        case ASTCENC_ERR_OUT_OF_MEM:    return "Out of memory";
        case ASTCENC_ERR_BAD_PARAM:     return "Bad parameter";
        case ASTCENC_ERR_BAD_BLOCK_SIZE: return "Bad block size";
        case ASTCENC_ERR_BAD_PROFILE:   return "Bad profile";
        case ASTCENC_ERR_NOT_IMPLEMENTED: return "Not implemented";
        case ASTCENC_ERR_BAD_FLAGS:     return "Bad flags";
        default:                        return "Unknown error";
    }
}
