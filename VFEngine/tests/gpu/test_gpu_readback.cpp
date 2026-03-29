#include <doctest.h>
#include <core/Device.hpp>
#include <core/Utilities.hpp>
#include <core/BufferUtilities.hpp>
#include <core/VulkanMemoryManager.hpp>
#include <core/Shader.hpp>
#include <core/PipelineUtilities.hpp>
#include <cstring>
#include <vector>

// ============================================================
// VK-1098: GPU buffer readback test infrastructure
//
// Level 2 testing — requires Vulkan initialized but no window.
// Uses headless Vulkan device (no surface/swapchain).
// Run with: Tests.exe --test-suite="GPU"
// Exclude with: Tests.exe --test-suite-exclude="GPU"
//
// NOTE: Tests within this suite run in declaration order.
// The last test case tears down the shared Vulkan device.
// ============================================================

namespace {

// Shared headless device for all GPU tests in this file.
// Initialized lazily on first use, torn down by the destructor
// (guaranteed via static storage duration) or explicitly by shutdownGPU().
struct HeadlessFixture
{
    std::unique_ptr<core::Device> device;

    HeadlessFixture()
    {
        device = std::make_unique<core::Device>(nullptr);
        device->init();
    }

    ~HeadlessFixture()
    {
        if (device)
        {
            device->getLogicalDevice().waitIdle();
            device->cleanUp();
        }
    }
};

static std::unique_ptr<HeadlessFixture> gpuFixture;

void ensureGPU()
{
    if (!gpuFixture)
    {
        gpuFixture = std::make_unique<HeadlessFixture>();
    }
}

void shutdownGPU()
{
    gpuFixture.reset();
}

} // anonymous namespace

TEST_SUITE("GPU") {

TEST_CASE("GPU: headless device initializes") {
    ensureGPU();
    auto& dev = gpuFixture->device;

    CHECK(dev->getLogicalDevice());
    CHECK(dev->getPhysicalDevice());
    CHECK(dev->getGraphicsQueue());
    CHECK(dev->getStagingCommandPool());
}

TEST_CASE("GPU: storage buffer create and readback") {
    ensureGPU();
    auto& dev = *gpuFixture->device;
    const vk::Device vkDevice = dev.getLogicalDevice();
    const vk::PhysicalDevice physDevice = dev.getPhysicalDevice();
    auto& memManager = dev.getMemoryManager();

    // Write test data to a host-visible buffer
    std::vector<float> testData = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    vk::DeviceSize bufferSize = testData.size() * sizeof(float);

    vk::Buffer buffer;
    core::VulkanAllocation allocation;
    core::BufferInfoRequest req(vkDevice, physDevice);
    req.size = bufferSize;
    req.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc;
    req.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    core::BufferUtilities::createBuffer(req, buffer, allocation, memManager);

    REQUIRE(allocation.isValid());
    REQUIRE(allocation.mappedPtr != nullptr);

    // Write data
    std::memcpy(allocation.mappedPtr, testData.data(), bufferSize);

    // Read back and verify
    std::vector<float> readback(testData.size());
    std::memcpy(readback.data(), allocation.mappedPtr, bufferSize);
    for (size_t i = 0; i < testData.size(); ++i)
    {
        CHECK(readback[i] == doctest::Approx(testData[i]));
    }

    core::BufferUtilities::destroyBuffer(vkDevice, buffer, allocation, memManager);
}

TEST_CASE("GPU: compute shader dispatch and readback") {
    ensureGPU();
    auto& dev = *gpuFixture->device;
    const vk::Device vkDevice = dev.getLogicalDevice();
    const vk::PhysicalDevice physDevice = dev.getPhysicalDevice();
    const vk::Queue queue = dev.getGraphicsQueue();
    const vk::CommandPool cmdPool = dev.getStagingCommandPool();
    auto& memManager = dev.getMemoryManager();

    // Compile a simple compute shader that doubles input values
    auto shader = std::make_unique<core::Shader>(dev);
    bool compiled = shader->compileFromSource(R"(
#type COMPUTE
#version 450

layout(local_size_x = 8, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0, std430) buffer InputBuffer  { float inputData[];  };
layout(binding = 1, std430) buffer OutputBuffer { float outputData[]; };

void main() {
    uint idx = gl_GlobalInvocationID.x;
    outputData[idx] = inputData[idx] * 2.0;
}
    )", "test_double_compute");

    REQUIRE(compiled);
    const auto& stages = shader->getShaderStages();
    REQUIRE(stages.size() == 1);
    REQUIRE(stages[0].stage == vk::ShaderStageFlagBits::eCompute);

    // --- Create descriptor set layout (2 storage buffers, plain layout) ---
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo dsLayoutInfo{};
    dsLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dsLayoutInfo.pBindings = bindings.data();
    vk::DescriptorSetLayout dsLayout = vkDevice.createDescriptorSetLayout(dsLayoutInfo);

    // --- Pipeline layout ---
    vk::PipelineLayoutCreateInfo plLayoutInfo{};
    plLayoutInfo.setLayoutCount = 1;
    plLayoutInfo.pSetLayouts = &dsLayout;
    vk::PipelineLayout pipelineLayout = vkDevice.createPipelineLayout(plLayoutInfo);

    // --- Compute pipeline ---
    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stages[0];
    pipelineInfo.layout = pipelineLayout;
    vk::Pipeline pipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
    REQUIRE(pipeline);

    // --- Descriptor pool + set (plain pool, no UpdateAfterBind) ---
    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageBuffer, 2};
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vk::DescriptorPool pool = vkDevice.createDescriptorPool(poolInfo);

    vk::DescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.descriptorPool = pool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &dsLayout;
    vk::DescriptorSet ds = vkDevice.allocateDescriptorSets(dsAllocInfo)[0];

    // --- Create input buffer (host-visible, write directly) ---
    constexpr uint32_t elementCount = 8;
    std::vector<float> inputData = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    vk::DeviceSize bufferSize = elementCount * sizeof(float);

    vk::Buffer inputBuffer;
    core::VulkanAllocation inputAlloc;
    core::BufferInfoRequest inputReq(vkDevice, physDevice);
    inputReq.size = bufferSize;
    inputReq.usage = vk::BufferUsageFlagBits::eStorageBuffer;
    inputReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    core::BufferUtilities::createBuffer(inputReq, inputBuffer, inputAlloc, memManager);
    REQUIRE(inputAlloc.mappedPtr != nullptr);
    std::memcpy(inputAlloc.mappedPtr, inputData.data(), bufferSize);

    // --- Create output buffer (host-visible for readback) ---
    vk::Buffer outputBuffer;
    core::VulkanAllocation outputAlloc;
    core::BufferInfoRequest outputReq(vkDevice, physDevice);
    outputReq.size = bufferSize;
    outputReq.usage = vk::BufferUsageFlagBits::eStorageBuffer;
    outputReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    core::BufferUtilities::createBuffer(outputReq, outputBuffer, outputAlloc, memManager);

    // --- Update descriptors ---
    vk::DescriptorBufferInfo inputBufInfo{inputBuffer, 0, bufferSize};
    vk::DescriptorBufferInfo outputBufInfo{outputBuffer, 0, bufferSize};

    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &inputBufInfo;
    writes[1].dstSet = ds;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &outputBufInfo;
    vkDevice.updateDescriptorSets(writes, {});

    // --- Dispatch compute ---
    auto cmd = core::Utilities::beginSingleTimeCommands(vkDevice, cmdPool);
    cmd->bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
    cmd->bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, ds, {});
    cmd->dispatch(1, 1, 1); // 1 workgroup of 8 invocations
    core::Utilities::endSingleTimeCommands(queue, cmd);

    // endSingleTimeCommands calls queue.waitIdle(), ensuring execution is complete.
    // Both buffers use eHostCoherent, so no vkInvalidateMappedMemoryRanges is needed —
    // writes from the GPU are automatically visible to the host after the queue is idle.

    // --- Readback and verify ---
    REQUIRE(outputAlloc.mappedPtr != nullptr);
    std::vector<float> result(elementCount);
    std::memcpy(result.data(), outputAlloc.mappedPtr, bufferSize);

    for (uint32_t i = 0; i < elementCount; ++i)
    {
        CHECK(result[i] == doctest::Approx(inputData[i] * 2.0f));
    }

    // --- Cleanup ---
    vkDevice.waitIdle();
    core::BufferUtilities::destroyBuffer(vkDevice, inputBuffer, inputAlloc, memManager);
    core::BufferUtilities::destroyBuffer(vkDevice, outputBuffer, outputAlloc, memManager);
    vkDevice.destroyPipeline(pipeline);
    vkDevice.destroyPipelineLayout(pipelineLayout);
    vkDevice.destroyDescriptorSetLayout(dsLayout);
    vkDevice.destroyDescriptorPool(pool);
    shader->cleanUp();
}

TEST_CASE("GPU: cleanup headless device") {
    shutdownGPU();
}

} // TEST_SUITE
