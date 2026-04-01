#include "SnowController.hpp"

namespace services
{
    PrecipitationConfig SnowController::getConfig() const
    {
        return {
            .maxSpawnRate = 30000.0f,
            .baseSize = 0.2f,
            .baseFallSpeed = 3.0f,
            .activationThreshold = 0.01f,
            .renderMode = 0,  // Billboard
            .coneSpread = 0.5f,
            .startColor = {1.0f, 1.0f, 1.0f, 0.9f},
            .lifetime = 5.0f,
            .softParticleDist = 1.0f,
            .lightingInfluence = 0.3f,
            .collisionLifetimeLoss = 0.8f,
            .gravityStrength = 2.5f,
            .shapeDimensions = {25.0f, 15.0f, 25.0f},
            .windStrengthMult = 0.6f,
            .sizeIntensityMin = 0.6f,
            .sizeIntensityMax = 1.4f,
            .stretchMin = 1.0f,
            .stretchMax = 1.0f
        };
    }
}
