#include "ClusterDAGTraversalPipeline.hpp"
#include "ClusterBuffer.hpp"
#include "GPUDrivenTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"
#include <array>

// Windows defines MemoryBarrier as a macro
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    constexpr uint32_t DAG_WORKGROUP_SIZE = 64;

    ClusterDAGTraversalPipeline::ClusterDAGTraversalPipeline(core::Device& device)
        : device(device)
    {
    }

    ClusterDAGTraversalPipeline::~ClusterDAGTraversalPipeline()
    {
        cleanup();
    }

    void ClusterDAGTraversalPipeline::init()
    {
        if (initialized)
        {
            return;
        }

        loggerInfo("ClusterDAGTraversalPipeline: Initializing...");

        createDescriptorSetLayout();
        createPipelineLayout();
        createRootEnqueuePipeline();
        createTraversePipeline();
        createPrepareIndirectPipeline();
        createDescriptorPool();
        allocateDescriptorSet();
        createTraversalParamsBuffer();

        initialized = true;
        loggerInfo("ClusterDAGTraversalPipeline: Initialized successfully");
    }

    void ClusterDAGTraversalPipeline::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (traversalParamsBuffer)
        {
            vkDevice.destroyBuffer(traversalParamsBuffer);
            traversalParamsBuffer = nullptr;
        }
        if (traversalParamsBufferMemory)
        {
            vkDevice.freeMemory(traversalParamsBufferMemory);
            traversalParamsBufferMemory = nullptr;
        }

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (rootEnqueuePipeline)
        {
            vkDevice.destroyPipeline(rootEnqueuePipeline);
            rootEnqueuePipeline = nullptr;
        }

        if (traversePipeline)
        {
            vkDevice.destroyPipeline(traversePipeline);
            traversePipeline = nullptr;
        }

        if (prepareIndirectPipeline)
        {
            vkDevice.destroyPipeline(prepareIndirectPipeline);
            prepareIndirectPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (prepareIndirectPipelineLayout)
        {
            vkDevice.destroyPipelineLayout(prepareIndirectPipelineLayout);
            prepareIndirectPipelineLayout = nullptr;
        }

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        if (prepareIndirectDescriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(prepareIndirectDescriptorSetLayout);
            prepareIndirectDescriptorSetLayout = nullptr;
        }

        if (rootEnqueueShader)
        {
            rootEnqueueShader->cleanUp();
            rootEnqueueShader.reset();
        }

        if (traverseShader)
        {
            traverseShader->cleanUp();
            traverseShader.reset();
        }

        if (prepareIndirectShader)
        {
            prepareIndirectShader->cleanUp();
            prepareIndirectShader.reset();
        }

        cachedClusterBuffer = nullptr;
        initialized = false;
        loggerInfo("ClusterDAGTraversalPipeline: Cleaned up");
    }

    void ClusterDAGTraversalPipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Bindings match cluster_dag_root_enqueue.glsl and cluster_dag_traverse.glsl:
        // binding 0:  ObjectBuffer (storage, readonly)
        // binding 1:  CameraUBO (uniform)
        // binding 5:  hiZTexture (combined image sampler)
        // binding 6:  ClusterBuffer (storage, readonly)
        // binding 7:  ClusterChildBuffer (storage, readonly)
        // binding 8:  DAGHeaderBuffer (storage, readonly)
        // binding 9:  TraversalParamsBuffer (storage, readonly)
        // binding 10: TraversalStateBuffer (storage, read-write)
        // binding 11: WorkQueueA (storage, read-write)
        // binding 12: WorkQueueB (storage, read-write)
        // binding 13: SelectionBuffer (storage, writeonly)
        // binding 14: ObjectDrawIndexMap (storage, readonly)

        std::array<vk::DescriptorSetLayoutBinding, 12> bindings{};

        // Binding 0: Object buffer
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: Camera UBO
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 5: Hi-Z texture
        bindings[2].binding = 5;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 6: Cluster buffer
        bindings[3].binding = 6;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 7: Cluster child buffer
        bindings[4].binding = 7;
        bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 8: DAG header buffer
        bindings[5].binding = 8;
        bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 9: Traversal params buffer
        bindings[6].binding = 9;
        bindings[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 10: Traversal state buffer
        bindings[7].binding = 10;
        bindings[7].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[7].descriptorCount = 1;
        bindings[7].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 11: Work queue A
        bindings[8].binding = 11;
        bindings[8].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[8].descriptorCount = 1;
        bindings[8].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 12: Work queue B
        bindings[9].binding = 12;
        bindings[9].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[9].descriptorCount = 1;
        bindings[9].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 13: Selection buffer
        bindings[10].binding = 13;
        bindings[10].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[10].descriptorCount = 1;
        bindings[10].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 14: Object draw index map
        bindings[11].binding = 14;
        bindings[11].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[11].descriptorCount = 1;
        bindings[11].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        loggerInfo("ClusterDAGTraversalPipeline: Created descriptor set layout");
    }

    void ClusterDAGTraversalPipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
        loggerInfo("ClusterDAGTraversalPipeline: Created pipeline layout");
    }

    void ClusterDAGTraversalPipeline::createRootEnqueuePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        rootEnqueueShader = std::make_unique<core::Shader>(device);
        rootEnqueueShader->readShader("../../resources/shaders/gpudriven/cluster_dag_root_enqueue.glsl");

        const auto& stages = rootEnqueueShader->getShaderStages();
        if (stages.empty())
        {
            loggerError("ClusterDAGTraversalPipeline: Failed to load root enqueue shader: {}",
                        rootEnqueueShader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("ClusterDAGTraversalPipeline: Failed to create root enqueue pipeline");
            return;
        }

        rootEnqueuePipeline = result.value;
        loggerInfo("ClusterDAGTraversalPipeline: Created root enqueue pipeline");
    }

    void ClusterDAGTraversalPipeline::createTraversePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        traverseShader = std::make_unique<core::Shader>(device);
        traverseShader->readShader("../../resources/shaders/gpudriven/cluster_dag_traverse.glsl");

        const auto& stages = traverseShader->getShaderStages();
        if (stages.empty())
        {
            loggerError("ClusterDAGTraversalPipeline: Failed to load traverse shader: {}",
                        traverseShader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("ClusterDAGTraversalPipeline: Failed to create traverse pipeline");
            return;
        }

        traversePipeline = result.value;
        loggerInfo("ClusterDAGTraversalPipeline: Created traverse pipeline");
    }

    void ClusterDAGTraversalPipeline::createPrepareIndirectPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create descriptor set layout for prepare indirect shader
        // Binding 0: TraversalStateBuffer (storage, readonly)
        // Binding 1: IndirectCommandBuffer (storage, writeonly)
        std::array<vk::DescriptorSetLayoutBinding, 2> prepareBindings{};

        prepareBindings[0].binding = 0;
        prepareBindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        prepareBindings[0].descriptorCount = 1;
        prepareBindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        prepareBindings[1].binding = 1;
        prepareBindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        prepareBindings[1].descriptorCount = 1;
        prepareBindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo prepareLayoutInfo{};
        prepareLayoutInfo.bindingCount = static_cast<uint32_t>(prepareBindings.size());
        prepareLayoutInfo.pBindings = prepareBindings.data();

        prepareIndirectDescriptorSetLayout = vkDevice.createDescriptorSetLayout(prepareLayoutInfo);

        // Create pipeline layout
        vk::PipelineLayoutCreateInfo preparePipelineLayoutInfo{};
        preparePipelineLayoutInfo.setLayoutCount = 1;
        preparePipelineLayoutInfo.pSetLayouts = &prepareIndirectDescriptorSetLayout;
        preparePipelineLayoutInfo.pushConstantRangeCount = 0;
        preparePipelineLayoutInfo.pPushConstantRanges = nullptr;

        prepareIndirectPipelineLayout = vkDevice.createPipelineLayout(preparePipelineLayoutInfo);

        // Load and compile shader
        prepareIndirectShader = std::make_unique<core::Shader>(device);
        prepareIndirectShader->readShader("../../resources/shaders/gpudriven/prepare_dag_indirect.glsl");

        const auto& stages = prepareIndirectShader->getShaderStages();
        if (stages.empty())
        {
            loggerError("ClusterDAGTraversalPipeline: Failed to load prepare indirect shader: {}",
                        prepareIndirectShader->getLastCompilationError());
            return;
        }

        // Create compute pipeline
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = prepareIndirectPipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("ClusterDAGTraversalPipeline: Failed to create prepare indirect pipeline");
            return;
        }

        prepareIndirectPipeline = result.value;
        loggerInfo("ClusterDAGTraversalPipeline: Created prepare indirect pipeline");
    }

    void ClusterDAGTraversalPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};

        // Storage buffers: bindings 0, 6, 7, 8, 9, 10, 11, 12, 13, 14 = 10 for main set
        // Plus 2 for prepare indirect set (traversal state + indirect command)
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 12;

        // Uniform buffer: binding 1
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        // Combined image sampler: binding 5
        poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[2].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 2;  // Main set + prepare indirect set
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        loggerInfo("ClusterDAGTraversalPipeline: Created descriptor pool");
    }

    void ClusterDAGTraversalPipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Allocate main descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        // Allocate prepare indirect descriptor set
        vk::DescriptorSetAllocateInfo prepareAllocInfo{};
        prepareAllocInfo.descriptorPool = descriptorPool;
        prepareAllocInfo.descriptorSetCount = 1;
        prepareAllocInfo.pSetLayouts = &prepareIndirectDescriptorSetLayout;

        auto prepareSets = vkDevice.allocateDescriptorSets(prepareAllocInfo);
        prepareIndirectDescriptorSet = prepareSets[0];

        loggerInfo("ClusterDAGTraversalPipeline: Allocated descriptor sets");
    }

    void ClusterDAGTraversalPipeline::createTraversalParamsBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                vkDevice, device.getPhysicalDevice(),
                sizeof(GPUClusterTraversalParams),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            traversalParamsBuffer,
            traversalParamsBufferMemory
        );

        loggerInfo("ClusterDAGTraversalPipeline: Created traversal params buffer");
    }

    void ClusterDAGTraversalPipeline::updateDescriptors(
        vk::Buffer objectBuffer,
        vk::Buffer cameraBuffer,
        vk::Buffer objectDrawIndexBuffer,
        ClusterBuffer& clusterBuffer)
    {
        if (cachedObjectBuffer == objectBuffer &&
            cachedCameraBuffer == cameraBuffer &&
            cachedObjectDrawIndexBuffer == objectDrawIndexBuffer &&
            cachedClusterBuffer == &clusterBuffer &&
            !descriptorsNeedUpdate)
        {
            return;
        }

        cachedObjectBuffer = objectBuffer;
        cachedCameraBuffer = cameraBuffer;
        cachedObjectDrawIndexBuffer = objectDrawIndexBuffer;
        cachedClusterBuffer = &clusterBuffer;
        descriptorsNeedUpdate = true;
    }

    void ClusterDAGTraversalPipeline::updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler)
    {
        if (cachedHiZView == hiZView && cachedHiZSampler == hiZSampler)
        {
            return;
        }

        cachedHiZView = hiZView;
        cachedHiZSampler = hiZSampler;
        descriptorsNeedUpdate = true;
    }

    void ClusterDAGTraversalPipeline::setTraversalParams(
        float projectionFactor,
        float screenErrorThreshold,
        float errorMultiplier,
        uint32_t frameIndex,
        bool enableCulling,
        bool enableOcclusion)
    {
        cachedParams.projectionFactor = projectionFactor;
        cachedParams.screenErrorThreshold = screenErrorThreshold;
        cachedParams.errorMultiplier = errorMultiplier;
        cachedParams.frameIndex = frameIndex;
        cachedParams.enableCulling = enableCulling ? 1u : 0u;
        cachedParams.enableOcclusion = enableOcclusion ? 1u : 0u;
        cachedParams.maxClustersToSelect = MAX_CLUSTER_SELECTIONS_PER_FRAME;
        cachedParams.traversalMode = CLUSTER_TRAVERSE_TOP_DOWN;
        cachedParams.targetTriangleCount = 0;  // Not used in current implementation
        cachedParams.maxTriangleCount = 0;     // Not used in current implementation
        cachedParams.currentSelectedCount = 0;
        cachedParams.maxWorkQueueEntries = MAX_WORK_QUEUE_ENTRIES;
    }

    void ClusterDAGTraversalPipeline::writeDescriptors()
    {
        if (!descriptorsNeedUpdate || !cachedClusterBuffer)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Object buffer (binding 0)
        vk::DescriptorBufferInfo objectInfo{};
        objectInfo.buffer = cachedObjectBuffer;
        objectInfo.offset = 0;
        objectInfo.range = VK_WHOLE_SIZE;

        // Camera buffer (binding 1)
        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cachedCameraBuffer;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUCameraData);

        // Hi-Z texture (binding 5)
        vk::DescriptorImageInfo hiZInfo{};
        hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        hiZInfo.imageView = cachedHiZView;
        hiZInfo.sampler = cachedHiZSampler;
        bool hasHiZ = cachedHiZView && cachedHiZSampler;

        // Cluster buffer (binding 6)
        vk::DescriptorBufferInfo clusterInfo{};
        clusterInfo.buffer = cachedClusterBuffer->getClusterBuffer();
        clusterInfo.offset = 0;
        clusterInfo.range = VK_WHOLE_SIZE;

        // Cluster child buffer (binding 7)
        vk::DescriptorBufferInfo childInfo{};
        childInfo.buffer = cachedClusterBuffer->getClusterChildBuffer();
        childInfo.offset = 0;
        childInfo.range = VK_WHOLE_SIZE;

        // DAG header buffer (binding 8)
        vk::DescriptorBufferInfo headerInfo{};
        headerInfo.buffer = cachedClusterBuffer->getDAGHeaderBuffer();
        headerInfo.offset = 0;
        headerInfo.range = VK_WHOLE_SIZE;

        // Traversal params buffer (binding 9)
        vk::DescriptorBufferInfo paramsInfo{};
        paramsInfo.buffer = traversalParamsBuffer;
        paramsInfo.offset = 0;
        paramsInfo.range = sizeof(GPUClusterTraversalParams);

        // Traversal state buffer (binding 10)
        vk::DescriptorBufferInfo stateInfo{};
        stateInfo.buffer = cachedClusterBuffer->getTraversalStateBuffer();
        stateInfo.offset = 0;
        stateInfo.range = sizeof(GPUDAGTraversalState);

        // Work queue A (binding 11)
        vk::DescriptorBufferInfo queueAInfo{};
        queueAInfo.buffer = cachedClusterBuffer->getWorkQueueBufferA();
        queueAInfo.offset = 0;
        queueAInfo.range = VK_WHOLE_SIZE;

        // Work queue B (binding 12)
        vk::DescriptorBufferInfo queueBInfo{};
        queueBInfo.buffer = cachedClusterBuffer->getWorkQueueBufferB();
        queueBInfo.offset = 0;
        queueBInfo.range = VK_WHOLE_SIZE;

        // Selection buffer (binding 13)
        vk::DescriptorBufferInfo selectionInfo{};
        selectionInfo.buffer = cachedClusterBuffer->getClusterSelectionBuffer();
        selectionInfo.offset = 0;
        selectionInfo.range = VK_WHOLE_SIZE;

        // Object draw index map (binding 14)
        vk::DescriptorBufferInfo drawIndexInfo{};
        drawIndexInfo.buffer = cachedObjectDrawIndexBuffer;
        drawIndexInfo.offset = 0;
        drawIndexInfo.range = VK_WHOLE_SIZE;

        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(12);

        // Binding 0: Object buffer
        vk::WriteDescriptorSet objectWrite{};
        objectWrite.dstSet = descriptorSet;
        objectWrite.dstBinding = 0;
        objectWrite.dstArrayElement = 0;
        objectWrite.descriptorCount = 1;
        objectWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        objectWrite.pBufferInfo = &objectInfo;
        writes.push_back(objectWrite);

        // Binding 1: Camera UBO
        vk::WriteDescriptorSet cameraWrite{};
        cameraWrite.dstSet = descriptorSet;
        cameraWrite.dstBinding = 1;
        cameraWrite.dstArrayElement = 0;
        cameraWrite.descriptorCount = 1;
        cameraWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        cameraWrite.pBufferInfo = &cameraInfo;
        writes.push_back(cameraWrite);

        // Binding 5: Hi-Z texture
        if (hasHiZ)
        {
            vk::WriteDescriptorSet hiZWrite{};
            hiZWrite.dstSet = descriptorSet;
            hiZWrite.dstBinding = 5;
            hiZWrite.dstArrayElement = 0;
            hiZWrite.descriptorCount = 1;
            hiZWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            hiZWrite.pImageInfo = &hiZInfo;
            writes.push_back(hiZWrite);
        }

        // Binding 6: Cluster buffer
        vk::WriteDescriptorSet clusterWrite{};
        clusterWrite.dstSet = descriptorSet;
        clusterWrite.dstBinding = 6;
        clusterWrite.dstArrayElement = 0;
        clusterWrite.descriptorCount = 1;
        clusterWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        clusterWrite.pBufferInfo = &clusterInfo;
        writes.push_back(clusterWrite);

        // Binding 7: Cluster child buffer
        vk::WriteDescriptorSet childWrite{};
        childWrite.dstSet = descriptorSet;
        childWrite.dstBinding = 7;
        childWrite.dstArrayElement = 0;
        childWrite.descriptorCount = 1;
        childWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        childWrite.pBufferInfo = &childInfo;
        writes.push_back(childWrite);

        // Binding 8: DAG header buffer
        vk::WriteDescriptorSet headerWrite{};
        headerWrite.dstSet = descriptorSet;
        headerWrite.dstBinding = 8;
        headerWrite.dstArrayElement = 0;
        headerWrite.descriptorCount = 1;
        headerWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        headerWrite.pBufferInfo = &headerInfo;
        writes.push_back(headerWrite);

        // Binding 9: Traversal params buffer
        vk::WriteDescriptorSet paramsWrite{};
        paramsWrite.dstSet = descriptorSet;
        paramsWrite.dstBinding = 9;
        paramsWrite.dstArrayElement = 0;
        paramsWrite.descriptorCount = 1;
        paramsWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        paramsWrite.pBufferInfo = &paramsInfo;
        writes.push_back(paramsWrite);

        // Binding 10: Traversal state buffer
        vk::WriteDescriptorSet stateWrite{};
        stateWrite.dstSet = descriptorSet;
        stateWrite.dstBinding = 10;
        stateWrite.dstArrayElement = 0;
        stateWrite.descriptorCount = 1;
        stateWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        stateWrite.pBufferInfo = &stateInfo;
        writes.push_back(stateWrite);

        // Binding 11: Work queue A
        vk::WriteDescriptorSet queueAWrite{};
        queueAWrite.dstSet = descriptorSet;
        queueAWrite.dstBinding = 11;
        queueAWrite.dstArrayElement = 0;
        queueAWrite.descriptorCount = 1;
        queueAWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        queueAWrite.pBufferInfo = &queueAInfo;
        writes.push_back(queueAWrite);

        // Binding 12: Work queue B
        vk::WriteDescriptorSet queueBWrite{};
        queueBWrite.dstSet = descriptorSet;
        queueBWrite.dstBinding = 12;
        queueBWrite.dstArrayElement = 0;
        queueBWrite.descriptorCount = 1;
        queueBWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        queueBWrite.pBufferInfo = &queueBInfo;
        writes.push_back(queueBWrite);

        // Binding 13: Selection buffer
        vk::WriteDescriptorSet selectionWrite{};
        selectionWrite.dstSet = descriptorSet;
        selectionWrite.dstBinding = 13;
        selectionWrite.dstArrayElement = 0;
        selectionWrite.descriptorCount = 1;
        selectionWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        selectionWrite.pBufferInfo = &selectionInfo;
        writes.push_back(selectionWrite);

        // Binding 14: Object draw index map
        vk::WriteDescriptorSet drawIndexWrite{};
        drawIndexWrite.dstSet = descriptorSet;
        drawIndexWrite.dstBinding = 14;
        drawIndexWrite.dstArrayElement = 0;
        drawIndexWrite.descriptorCount = 1;
        drawIndexWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        drawIndexWrite.pBufferInfo = &drawIndexInfo;
        writes.push_back(drawIndexWrite);

        vkDevice.updateDescriptorSets(writes, {});
        descriptorsNeedUpdate = false;

        loggerInfo("ClusterDAGTraversalPipeline: Updated descriptors (Hi-Z: {})", hasHiZ ? "yes" : "no");

        // Also update prepare indirect descriptors
        writePrepareIndirectDescriptors();
    }

    void ClusterDAGTraversalPipeline::writePrepareIndirectDescriptors()
    {
        if (!cachedClusterBuffer)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Binding 0: Traversal state buffer
        vk::DescriptorBufferInfo stateInfo{};
        stateInfo.buffer = cachedClusterBuffer->getTraversalStateBuffer();
        stateInfo.offset = 0;
        stateInfo.range = sizeof(GPUDAGTraversalState);

        // Binding 1: Indirect draw command buffer
        vk::DescriptorBufferInfo indirectInfo{};
        indirectInfo.buffer = cachedClusterBuffer->getIndirectDrawCommandBuffer();
        indirectInfo.offset = 0;
        indirectInfo.range = 12;  // MeshTasksIndirectCommand: 3 * uint32

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = prepareIndirectDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &stateInfo;

        writes[1].dstSet = prepareIndirectDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &indirectInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void ClusterDAGTraversalPipeline::uploadTraversalParams(vk::CommandBuffer cmd)
    {
        // Use vkCmdUpdateBuffer for small buffer updates
        cmd.updateBuffer(traversalParamsBuffer, 0, sizeof(GPUClusterTraversalParams), &cachedParams);
    }

    void ClusterDAGTraversalPipeline::dispatch(
        vk::CommandBuffer cmd,
        uint32_t objectCount,
        ClusterBuffer& clusterBuffer)
    {
        if (!initialized || objectCount == 0)
        {
            return;
        }

        writeDescriptors();

        // Reset traversal state for new frame
        GPUDAGTraversalState resetState{};
        cmd.updateBuffer(clusterBuffer.getTraversalStateBuffer(), 0, sizeof(GPUDAGTraversalState), &resetState);

        // Upload traversal params
        uploadTraversalParams(cmd);

        // Barrier after buffer updates
        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &memBarrier,
            0, nullptr,
            0, nullptr
        );

        // Bind descriptor set (used by both pipelines)
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

        // =========================================================================
        // Phase 1: Root Enqueue - Initialize work queue with root clusters
        // =========================================================================
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, rootEnqueuePipeline);

        uint32_t rootGroupCount = (objectCount + DAG_WORKGROUP_SIZE - 1) / DAG_WORKGROUP_SIZE;
        cmd.dispatch(rootGroupCount, 1, 1);

        // Barrier after root enqueue
        memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &memBarrier,
            0, nullptr,
            0, nullptr
        );

        // =========================================================================
        // Phase 2: Multi-pass Traversal
        // =========================================================================
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, traversePipeline);

        // Note: In a more sophisticated implementation, we would read back the queue count
        // from the GPU and dispatch only as many threads as needed. For now, we dispatch
        // a conservative maximum and let threads early-exit.

        for (uint32_t pass = 0; pass < maxPasses; ++pass)
        {
            // Dispatch traverse shader
            // Use max work queue entries as conservative upper bound
            uint32_t traverseGroupCount = (MAX_WORK_QUEUE_ENTRIES + DAG_WORKGROUP_SIZE - 1) / DAG_WORKGROUP_SIZE;
            cmd.dispatch(traverseGroupCount, 1, 1);

            // Update pass index in traversal state (using vkCmdUpdateBuffer)
            // The shader reads passIndex to determine which queue to read/write (ping-pong)
            uint32_t nextPassIndex = pass + 1;
            cmd.updateBuffer(
                clusterBuffer.getTraversalStateBuffer(),
                offsetof(GPUDAGTraversalState, passIndex),
                sizeof(uint32_t),
                &nextPassIndex
            );

            // Swap input/output queue counts for next pass
            // Reset outputQueueCount to 0, copy outputQueueCount to inputQueueCount
            // This is tricky without GPU readback - for now we just do barrier
            // The shader handles the swap internally based on passIndex % 2

            // Barrier between passes
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlags{},
                1, &memBarrier,
                0, nullptr,
                0, nullptr
            );
        }

        // =========================================================================
        // Phase 3: Prepare Indirect Dispatch Command (VK-300)
        // Read selectedCount from traversal state and write to indirect command buffer
        // =========================================================================
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, prepareIndirectPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, prepareIndirectPipelineLayout, 0,
                               prepareIndirectDescriptorSet, {});

        // Dispatch single invocation to prepare the indirect command
        cmd.dispatch(1, 1, 1);

        // Barrier: prepare indirect writes must complete before draw indirect reads
        vk::MemoryBarrier indirectBarrier{
            vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eIndirectCommandRead
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eDrawIndirect,
            vk::DependencyFlags{},
            1, &indirectBarrier,
            0, nullptr,
            0, nullptr
        );
    }

} // namespace render::gpudriven
