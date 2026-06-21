#pragma once

#include <cstdint>

namespace services
{
    // Opaque handle to a running VFX combo (a parent that owns N child VFXInstanceIds).
    // Distinct type from VFXInstanceId so the per-instance and combo APIs can't be crossed.
    // 0 == invalid.
    using VFXComboInstanceId = uint32_t;
}
