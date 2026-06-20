#include "PluginPostProcessEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "resource/PathResolver.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cstring>

namespace render::postprocess
{
    namespace
    {
        // Push-constant ranges must be 4-byte aligned (VUID-VkPushConstantRange-size-00297).
        uint32_t align4(uint32_t v) { return (v + 3u) & ~3u; }
    }

    PluginPostProcessEffect::PluginPostProcessEffect(core::Device& device,
                                                     std::string fragmentGlsl,
                                                     uint32_t priority,
                                                     uint32_t paramsSize,
                                                     bool startEnabled,
                                                     std::string debugName)
        : device{device}, fragmentGlsl{std::move(fragmentGlsl)}, priority{priority},
          paramsSize{paramsSize}, debugName{std::move(debugName)}
    {
        enabled = startEnabled;
        paramBytes.assign(align4(paramsSize), std::byte{0});
    }

    std::string PluginPostProcessEffect::synthesizeSource() const
    {
        // Prepend the engine's fullscreen-triangle vertex stage; the plugin
        // supplies only the fragment stage (with its own #version).
        std::string src;
        src.reserve(fragmentGlsl.size() + 160);
        src += "#type VERTEX\n";
        src += "#version 460 core\n";
        src += "#extension GL_GOOGLE_include_directive : require\n";
        src += "#include \"postprocess/fullscreen_vert.glsl\"\n";
        src += "#type FRAGMENT\n";
        src += fragmentGlsl;
        if (!fragmentGlsl.empty() && fragmentGlsl.back() != '\n')
            src += '\n';
        return src;
    }

    void PluginPostProcessEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        // Own input descriptor-set layout — identical bindings to
        // PostProcessPipeline's, so the sets it binds at record() are compatible.
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        inputDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        shader = std::make_shared<core::Shader>(device);
        shader->setIncludeBasePath(resource::PathResolver::resolveEnginePath("../../resources/shaders"));
        if (!shader->compileFromSource(synthesizeSource(),
                                       debugName.empty() ? "plugin_postprocess" : debugName))
        {
            vfLogError("PluginPostProcessEffect '{}': shader compilation failed: {}",
                       debugName, shader->getLastCompilationError());
            shader.reset();
            initialized = false;
            return; // left uninitialized — execute() skips it
        }

        if (!buildPipeline(colorFormat, extent))
        {
            initialized = false;
            return;
        }

        initialized = true;
    }

    bool PluginPostProcessEffect::buildPipeline(vk::Format colorFormat, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {colorFormat};
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout};
        config.pushConstantSize = align4(paramsSize); // 0 => no push-constant range
        config.pushConstantStages = vk::ShaderStageFlagBits::eFragment;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        if (!result.pipeline)
        {
            vfLogError("PluginPostProcessEffect '{}': pipeline creation failed", debugName);
            return false;
        }
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
        return true;
    }

    void PluginPostProcessEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            dev.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }
        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (shader) // only rebuild the pipeline; the compiled shader survives a resize
            buildPipeline(colorFormat, extent);
    }

    void PluginPostProcessEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            dev.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }
        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }
        if (inputDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(inputDescriptorSetLayout);
            inputDescriptorSetLayout = nullptr;
        }
        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }

    void PluginPostProcessEffect::record(const vk::CommandBuffer& commandBuffer,
                                         vk::DescriptorSet inputDescriptorSet)
    {
        if (!graphicsPipeline)
            return;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                         0, inputDescriptorSet, nullptr);

        if (!paramBytes.empty())
        {
            commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                        0, static_cast<uint32_t>(paramBytes.size()),
                                        paramBytes.data());
        }

        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::PostProcess);
    }

    void PluginPostProcessEffect::setParams(const std::byte* data, size_t size)
    {
        if (paramBytes.empty() || data == nullptr)
            return;
        std::memcpy(paramBytes.data(), data, std::min(size, paramBytes.size()));
    }
}
