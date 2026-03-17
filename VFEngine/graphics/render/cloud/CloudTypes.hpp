#pragma once
#include "cloud/CloudSettings.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace render::cloud
{
    // GPU-side parameter UBO -- all vec4/mat4 to guarantee std140 alignment
    struct GPUCloudParams
    {
        glm::vec4 cloudLayer;        // x=minAlt, y=maxAlt, z=thickness, w=planetRadius
        glm::vec4 cloudDensity;      // x=globalDensity, y=globalCoverage, z=0, w=cloudType
        glm::vec4 cloudShaping;      // x=shapeScale, y=detailScale, z=erosionStrength, w=curlStrength
        glm::vec4 windParams;        // xyz=windDir*speed, w=timeOffset
        glm::vec4 lightParams;       // x=lightAbsorption, y=phaseG1, z=phaseG2, w=phaseBlend
        glm::vec4 lightColor;        // xyz=sunIrradiance, w=ambientIntensity
        glm::vec4 sunDirection;      // xyz=sunDir, w=0
        glm::vec4 cameraPosition;    // xyz=pos, w=0
        glm::mat4 invViewProjection;
        glm::mat4 prevViewProjection;
        glm::vec4 screenParams;      // x=width, y=height, z=near, w=far
        glm::vec4 temporalParams;    // x=blendFactor, y=frameIndex, z=0, w=0
        glm::vec4 atmosphereParams;  // x=planetRadius, y=atmosphereRadius, z=0, w=0
        glm::vec4 marchParams;       // x=maxSteps, y=lightSteps, z=0, w=0
    };
}
