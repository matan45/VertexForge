// Keep in sync with VFEngine/utilities/vfx/VFXVariance.hpp.

const uint VFX_VAR_STREAM_SIZE = 0xA341316Cu;
const uint VFX_VAR_STREAM_LIFETIME = 0xC8013EA4u;
const uint VFX_VAR_STREAM_SPEED = 0xAD90777Du;
const uint VFX_VAR_STREAM_ROTATION = 0x7E95761Eu;
const uint VFX_VAR_STREAM_ANGULAR_VELOCITY = 0x9E3779B9u;
const uint VFX_VAR_STREAM_COLOR_VALUE = 0xBB67AE85u;
const uint VFX_VAR_STREAM_ALPHA = 0x3C6EF372u;

uint vfxPcgHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float vfxVarianceUnit(uint seed, uint stream)
{
    return float(vfxPcgHash(seed ^ stream)) / float(0xFFFFFFFFu);
}

float vfxVarianceSigned(uint seed, uint stream)
{
    return vfxVarianceUnit(seed, stream) * 2.0 - 1.0;
}
