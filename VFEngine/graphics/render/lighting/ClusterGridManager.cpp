#include "ClusterGridManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace render::lighting
{
    ClusterGridManager::ClusterGridManager(core::Device& device)
        : device(device)
    {
    }

    ClusterGridManager::~ClusterGridManager()
    {
        cleanup();
    }

    void ClusterGridManager::init(const ClusterGridConfig& gridConfig)
    {
        if (initialized)
        {
            loggerWarning("ClusterGridManager: Already initialized");
            return;
        }

        config = gridConfig;

        // Validate grid dimensions to prevent division by zero
        if (config.tilesX == 0 || config.tilesY == 0 || config.slicesZ == 0)
        {
            loggerError("ClusterGridManager: Invalid grid dimensions ({}x{}x{}) - all must be > 0",
                        config.tilesX, config.tilesY, config.slicesZ);
            return;
        }

        uint32_t totalClusters = config.getTotalClusters();

        if (totalClusters > ClusterConstants::MAX_CLUSTERS)
        {
            loggerWarning("ClusterGridManager: Requested {} clusters exceeds max {}. Clamping.",
                          totalClusters, ClusterConstants::MAX_CLUSTERS);
            // Reduce slicesZ to fit within limit
            config.slicesZ = ClusterConstants::MAX_CLUSTERS / (config.tilesX * config.tilesY);
            totalClusters = config.getTotalClusters();
        }

        cpuClusterAABBs.resize(totalClusters);

        loggerInfo("ClusterGridManager: Initializing with {}x{}x{} = {} clusters",
                   config.tilesX, config.tilesY, config.slicesZ, totalClusters);

        createBuffers();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptors();

        initialized = true;
        loggerInfo("ClusterGridManager: Initialized successfully");
    }

    void ClusterGridManager::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        destroyBuffers();

        cpuClusterAABBs.clear();

        initialized = false;
        loggerInfo("ClusterGridManager: Cleaned up");
    }

    void ClusterGridManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        uint32_t totalClusters = config.getTotalClusters();

        {
            vk::DeviceSize bufferSize = sizeof(GPUClusterGridParams);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, paramsBuffer, paramsMemory);

            paramsMapped = logicalDevice.mapMemory(paramsMemory, 0, bufferSize, vk::MemoryMapFlags{});
            std::memset(paramsMapped, 0, sizeof(GPUClusterGridParams));
        }

        {
            vk::DeviceSize bufferSize = totalClusters * sizeof(GPUClusterAABB);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, clusterAABBBuffer, clusterAABBMemory);

            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, clusterAABBStagingBuffer, clusterAABBStagingMemory);

            clusterAABBStagingMapped = logicalDevice.mapMemory(clusterAABBStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        loggerInfo("ClusterGridManager: Created cluster grid buffers (~{} KB)",
                   (sizeof(GPUClusterGridParams) + totalClusters * sizeof(GPUClusterAABB) * 2) / 1024);
    }

    void ClusterGridManager::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (paramsMapped)
        {
            logicalDevice.unmapMemory(paramsMemory);
            paramsMapped = nullptr;
        }
        if (clusterAABBStagingMapped)
        {
            logicalDevice.unmapMemory(clusterAABBStagingMemory);
            clusterAABBStagingMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, clusterAABBStagingBuffer, clusterAABBStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, clusterAABBBuffer, clusterAABBMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, paramsBuffer, paramsMemory);
    }

    void ClusterGridManager::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        // Binding 0: ClusterGridParams UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: ClusterAABBs SSBO
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[1].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        loggerInfo("ClusterGridManager: Created descriptor set layout");
    }

    void ClusterGridManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        loggerInfo("ClusterGridManager: Created descriptor pool");
    }

    void ClusterGridManager::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        std::vector<vk::DescriptorSet> sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        loggerInfo("ClusterGridManager: Allocated descriptor set");
    }

    void ClusterGridManager::updateDescriptors()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};

        bufferInfos[0].buffer = paramsBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = sizeof(GPUClusterGridParams);

        bufferInfos[1].buffer = clusterAABBBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfos[0];

        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &bufferInfos[1];

        vkDevice.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    void ClusterGridManager::updateFromCamera(const ClusterCameraParams& cameraParams)
    {
        if (!initialized)
        {
            return;
        }

        if (detectCameraChanges(cameraParams))
        {
            cachedCameraParams = cameraParams;
            needsRebuild = true;
        }

        if (needsRebuild)
        {
            rebuildClusterGrid();
            needsRebuild = false;
            needsUpload = true;
        }
    }

    bool ClusterGridManager::detectCameraChanges(const ClusterCameraParams& newParams) const
    {
        constexpr float EPSILON = 0.0001f;
        return std::abs(cachedCameraParams.nearPlane - newParams.nearPlane) > EPSILON
            || std::abs(cachedCameraParams.farPlane - newParams.farPlane) > EPSILON
            || std::abs(cachedCameraParams.fovY - newParams.fovY) > EPSILON
            || std::abs(cachedCameraParams.aspectRatio - newParams.aspectRatio) > EPSILON
            || cachedCameraParams.screenWidth != newParams.screenWidth
            || cachedCameraParams.screenHeight != newParams.screenHeight;
    }

    void ClusterGridManager::rebuildClusterGrid()
    {
        computeClusterAABBs();
        updateParamsBuffer();
    }

    void ClusterGridManager::computeClusterAABBs()
    {
        const float zNear = cachedCameraParams.nearPlane;
        const float zFar = cachedCameraParams.farPlane;
        const float logFarNear = std::log(zFar / zNear);

        uint32_t totalClusters = config.getTotalClusters();
        cpuClusterAABBs.resize(totalClusters);

        // For each cluster, compute the view-space AABB from its frustum corners
        for (uint32_t z = 0; z < config.slicesZ; ++z)
        {
            // Logarithmic depth slice boundaries (better near-plane precision)
            float sliceNear = zNear * std::exp(static_cast<float>(z) / static_cast<float>(config.slicesZ) * logFarNear);
            float sliceFar = zNear * std::exp(static_cast<float>(z + 1) / static_cast<float>(config.slicesZ) * logFarNear);

            for (uint32_t y = 0; y < config.tilesY; ++y)
            {
                for (uint32_t x = 0; x < config.tilesX; ++x)
                {
                    // NDC corners of this tile
                    // In Vulkan NDC, X: [-1, 1] left to right, Y: [-1, 1] top to bottom
                    float ndcMinX = -1.0f + 2.0f * static_cast<float>(x) / static_cast<float>(config.tilesX);
                    float ndcMaxX = -1.0f + 2.0f * static_cast<float>(x + 1) / static_cast<float>(config.tilesX);
                    float ndcMinY = -1.0f + 2.0f * static_cast<float>(y) / static_cast<float>(config.tilesY);
                    float ndcMaxY = -1.0f + 2.0f * static_cast<float>(y + 1) / static_cast<float>(config.tilesY);

                    glm::vec3 corners[8];
                    corners[0] = unprojectToViewSpace(ndcMinX, ndcMinY, sliceNear);
                    corners[1] = unprojectToViewSpace(ndcMaxX, ndcMinY, sliceNear);
                    corners[2] = unprojectToViewSpace(ndcMinX, ndcMaxY, sliceNear);
                    corners[3] = unprojectToViewSpace(ndcMaxX, ndcMaxY, sliceNear);
                    corners[4] = unprojectToViewSpace(ndcMinX, ndcMinY, sliceFar);
                    corners[5] = unprojectToViewSpace(ndcMaxX, ndcMinY, sliceFar);
                    corners[6] = unprojectToViewSpace(ndcMinX, ndcMaxY, sliceFar);
                    corners[7] = unprojectToViewSpace(ndcMaxX, ndcMaxY, sliceFar);

                    glm::vec3 aabbMin = corners[0];
                    glm::vec3 aabbMax = corners[0];
                    for (int i = 1; i < 8; ++i)
                    {
                        aabbMin = glm::min(aabbMin, corners[i]);
                        aabbMax = glm::max(aabbMax, corners[i]);
                    }

                    // Store in linear index (Z-major for cache efficiency: depth slices contiguous)
                    uint32_t clusterIndex = z * config.tilesX * config.tilesY
                                          + y * config.tilesX
                                          + x;

                    cpuClusterAABBs[clusterIndex].minPoint = glm::vec4(aabbMin, 0.0f);
                    cpuClusterAABBs[clusterIndex].maxPoint = glm::vec4(aabbMax, 0.0f);
                }
            }
        }
    }

    glm::vec3 ClusterGridManager::unprojectToViewSpace(float ndcX, float ndcY, float viewZ) const
    {
        // In view-space, camera looks down -Z axis
        // viewZ is the positive depth value (distance from camera)

        // For a perspective projection:
        // x_view = x_ndc * z_view / P[0][0]
        // y_view = y_ndc * z_view / P[1][1]
        // z_view = -viewZ (negative because camera looks down -Z)

        const glm::mat4& proj = cachedCameraParams.projection;

        // Extract projection parameters
        float invP00 = 1.0f / proj[0][0];  // 1 / (2n/w) = w/(2n)
        float invP11 = 1.0f / proj[1][1];  // 1 / (2n/h) = h/(2n)

        // Compute view-space position
        // Note: viewZ is positive distance, but view-space Z is negative
        float zView = -viewZ;
        float xView = ndcX * (-zView) * invP00;
        float yView = ndcY * (-zView) * invP11;

        return glm::vec3(xView, yView, zView);
    }

    void ClusterGridManager::updateParamsBuffer()
    {
        if (!paramsMapped)
            return;

        const float zNear = cachedCameraParams.nearPlane;
        const float zFar = cachedCameraParams.farPlane;
        const float logFarNear = std::log(zFar / zNear);
        float screenWidth = static_cast<float>(cachedCameraParams.screenWidth);
        float screenHeight = static_cast<float>(cachedCameraParams.screenHeight);
        float tileSizeX = screenWidth / static_cast<float>(config.tilesX);
        float tileSizeY = screenHeight / static_cast<float>(config.tilesY);

        cpuParams.gridDimensions = glm::uvec4(config.tilesX, config.tilesY,
                                               config.slicesZ, config.getTotalClusters());
        cpuParams.screenParams = glm::vec4(screenWidth, screenHeight, tileSizeX, tileSizeY);
        cpuParams.depthParams = glm::vec4(zNear, zFar, logFarNear, 1.0f / logFarNear);
        cpuParams.invProjection = cachedCameraParams.invProjection;

        // Scale and bias for computing cluster index from screen position and depth
        cpuParams.clusterScale = glm::vec4(1.0f / tileSizeX, 1.0f / tileSizeY,
                                            static_cast<float>(config.slicesZ) / logFarNear, 0.0f);
        cpuParams.clusterBias = glm::vec4(0.0f, 0.0f,
                                           -static_cast<float>(config.slicesZ) * std::log(zNear) / logFarNear, 0.0f);

        std::memcpy(paramsMapped, &cpuParams, sizeof(GPUClusterGridParams));
    }

    static vk::BufferMemoryBarrier makeClusterBufferBarrier(
        vk::AccessFlags srcAccess, vk::AccessFlags dstAccess, vk::Buffer buffer)
    {
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        return barrier;
    }

    static constexpr vk::PipelineStageFlags CLUSTER_SHADER_STAGES =
        vk::PipelineStageFlagBits::eFragmentShader |
        vk::PipelineStageFlagBits::eComputeShader |
        vk::PipelineStageFlagBits::eMeshShaderEXT;

    void ClusterGridManager::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !needsUpload)
            return;

        uint32_t totalClusters = config.getTotalClusters();
        if (cpuClusterAABBs.size() != totalClusters)
        {
            loggerError("ClusterGridManager: AABB count mismatch ({} vs expected {})",
                        cpuClusterAABBs.size(), totalClusters);
            return;
        }

        size_t copySize = totalClusters * sizeof(GPUClusterAABB);
        std::memcpy(clusterAABBStagingMapped, cpuClusterAABBs.data(), copySize);

        auto preBarrier = makeClusterBufferBarrier(
            vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite, clusterAABBBuffer);
        cmd.pipelineBarrier(CLUSTER_SHADER_STAGES, vk::PipelineStageFlagBits::eTransfer,
                            {}, {}, preBarrier, {});

        vk::BufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = copySize;
        cmd.copyBuffer(clusterAABBStagingBuffer, clusterAABBBuffer, region);

        auto postBarrier = makeClusterBufferBarrier(
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, clusterAABBBuffer);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, CLUSTER_SHADER_STAGES,
                            {}, {}, postBarrier, {});

        needsUpload = false;
    }

    static bool sphereIntersectsAABB(const glm::vec3& center, float radius,
                                      const glm::vec3& aabbMin, const glm::vec3& aabbMax)
    {
        glm::vec3 closestPoint = glm::clamp(center, aabbMin, aabbMax);
        float distSq = glm::dot(closestPoint - center, closestPoint - center);
        return distSq <= radius * radius;
    }

    std::vector<uint32_t> ClusterGridManager::getClusterIndicesForPointLight(
        const glm::vec3& lightPosViewSpace,
        float radius) const
    {
        std::vector<uint32_t> result;

        if (!initialized || cpuClusterAABBs.empty())
        {
            return result;
        }

        result.reserve(64); // Typical number of affected clusters

        for (uint32_t i = 0; i < cpuClusterAABBs.size(); ++i)
        {
            const GPUClusterAABB& aabb = cpuClusterAABBs[i];
            glm::vec3 aabbMin = glm::vec3(aabb.minPoint);
            glm::vec3 aabbMax = glm::vec3(aabb.maxPoint);

            if (sphereIntersectsAABB(lightPosViewSpace, radius, aabbMin, aabbMax))
            {
                result.push_back(i);
            }
        }

        return result;
    }

    static bool pointInCone(const glm::vec3& point, const glm::vec3& apex,
                            const glm::vec3& dir, float range, float cosAngle)
    {
        glm::vec3 toPoint = point - apex;
        float dist = glm::length(toPoint);

        if (dist < 0.0001f)
        {
            return true;
        }
        if (dist > range)
        {
            return false;
        }

        float pointCosAngle = glm::dot(toPoint / dist, dir);
        return pointCosAngle >= cosAngle;
    }

    static glm::vec3 closestPointOnAABB(const glm::vec3& point,
                                         const glm::vec3& aabbMin, const glm::vec3& aabbMax)
    {
        return glm::clamp(point, aabbMin, aabbMax);
    }

    static bool coneAxisIntersectsAABB(const glm::vec3& apex, const glm::vec3& dir,
                                        float range, const glm::vec3& aabbMin, const glm::vec3& aabbMax)
    {
        glm::vec3 invDir = 1.0f / (dir + glm::vec3(0.0001f));

        glm::vec3 t1 = (aabbMin - apex) * invDir;
        glm::vec3 t2 = (aabbMax - apex) * invDir;

        glm::vec3 tMin = glm::min(t1, t2);
        glm::vec3 tMax = glm::max(t1, t2);

        float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
        float tFar = glm::min(glm::min(tMax.x, tMax.y), tMax.z);

        return tNear <= tFar && tFar >= 0.0f && tNear <= range;
    }

    static bool spotLightIntersectsCluster(
        const glm::vec3& lightPos, const glm::vec3& lightDir,
        float range, float outerAngleCos, float outerAngleSin,
        const glm::vec3& aabbMin, const glm::vec3& aabbMax)
    {
        glm::vec3 aabbCenter = (aabbMin + aabbMax) * 0.5f;

        // Test 1: AABB center inside cone
        if (pointInCone(aabbCenter, lightPos, lightDir, range, outerAngleCos))
            return true;

        // Test 2: Any AABB corner inside cone
        glm::vec3 corners[8] = {
            {aabbMin.x, aabbMin.y, aabbMin.z}, {aabbMax.x, aabbMin.y, aabbMin.z},
            {aabbMin.x, aabbMax.y, aabbMin.z}, {aabbMax.x, aabbMax.y, aabbMin.z},
            {aabbMin.x, aabbMin.y, aabbMax.z}, {aabbMax.x, aabbMin.y, aabbMax.z},
            {aabbMin.x, aabbMax.y, aabbMax.z}, {aabbMax.x, aabbMax.y, aabbMax.z}
        };

        for (const auto& corner : corners)
        {
            if (pointInCone(corner, lightPos, lightDir, range, outerAngleCos))
                return true;
        }

        // Test 3: Cone axis passes through AABB
        if (coneAxisIntersectsAABB(lightPos, lightDir, range, aabbMin, aabbMax))
            return true;

        // Test 4: Closest point on AABB to cone axis projection
        glm::vec3 toCenter = aabbCenter - lightPos;
        float projLen = glm::dot(toCenter, lightDir);
        if (projLen > 0.0f && projLen <= range)
        {
            glm::vec3 projPoint = lightPos + lightDir * projLen;
            glm::vec3 closestOnAABB = closestPointOnAABB(projPoint, aabbMin, aabbMax);
            float coneRadiusAtDepth = projLen * outerAngleSin / outerAngleCos;
            if (glm::length(closestOnAABB - projPoint) <= coneRadiusAtDepth)
                return true;
        }

        // Test 5: Edge midpoints inside cone
        constexpr int edgeIndices[12][2] = {
            {0,1}, {2,3}, {4,5}, {6,7},
            {0,2}, {1,3}, {4,6}, {5,7},
            {0,4}, {1,5}, {2,6}, {3,7}
        };
        for (const auto& edge : edgeIndices)
        {
            glm::vec3 edgeMid = (corners[edge[0]] + corners[edge[1]]) * 0.5f;
            if (pointInCone(edgeMid, lightPos, lightDir, range, outerAngleCos))
                return true;
        }

        return false;
    }

    std::vector<uint32_t> ClusterGridManager::getClusterIndicesForSpotLight(
        const glm::vec3& lightPosViewSpace,
        const glm::vec3& lightDirViewSpace,
        float range,
        float outerAngleCos) const
    {
        std::vector<uint32_t> result;

        if (!initialized || cpuClusterAABBs.empty())
            return result;

        result.reserve(32);
        float outerAngleSin = std::sqrt(1.0f - outerAngleCos * outerAngleCos);

        for (uint32_t i = 0; i < cpuClusterAABBs.size(); ++i)
        {
            const GPUClusterAABB& aabb = cpuClusterAABBs[i];
            glm::vec3 aabbMin = glm::vec3(aabb.minPoint);
            glm::vec3 aabbMax = glm::vec3(aabb.maxPoint);

            if (!sphereIntersectsAABB(lightPosViewSpace, range, aabbMin, aabbMax))
                continue;

            if (spotLightIntersectsCluster(lightPosViewSpace, lightDirViewSpace,
                                           range, outerAngleCos, outerAngleSin,
                                           aabbMin, aabbMax))
            {
                result.push_back(i);
            }
        }

        return result;
    }
}
