#include "RainController.hpp"

namespace services
{
    PrecipitationConfig RainController::getConfig() const
    {
        return {
            .maxSpawnRate = 50000.0f,
            .baseSize = 0.06f,
            .baseFallSpeed = 10.0f,
            .activationThreshold = 0.01f,
            .renderMode = 1,  // StretchedBillboard
            .coneSpread = 0.0f,
            .startColor = {0.7f, 0.75f, 0.8f, 0.3f},
            .lifetime = 2.0f,
            .softParticleDist = 0.5f,
            .lightingInfluence = 0.2f,
            .collisionLifetimeLoss = 1.0f,
            .gravityStrength = 9.81f,
            .shapeDimensions = {20.0f, 10.0f, 20.0f},
            .windStrengthMult = 0.3f,
            .sizeIntensityMin = 0.8f,
            .sizeIntensityMax = 1.5f,
            .stretchMin = 4.0f,
            .stretchMax = 8.0f
        };
    }
}
