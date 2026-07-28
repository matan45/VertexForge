// VK-1635: CPU vertex layout for the two text instance structs.
//
// Both structs grew three attributes (effectParams / effectColors / effectMargin) and both
// feed their vertex-input descriptions from offsetof. Nothing in the build reconciles those
// descriptions with the GLSL that consumes them, and Vulkan will happily bind a pipeline
// whose attribute offsets point at the wrong members - the failure surfaces as garbage
// colours or an outline that reacts to the wrong slider, with no validation error.
//
// So this pins the two things that would drift silently: the struct size (glm is packed in
// this build, and defining GLM_FORCE_DEFAULT_ALIGNED_GENTYPES anywhere would quietly repad
// every member), and the attribute table being monotonic, non-overlapping and in-stride
// with offsets that really do come from the named members.

#include <doctest.h>

#include <render/text/TextTypes.hpp>
#include <render/ui/UITextRenderTypes.hpp>

#include <cstddef>

namespace
{
    // Bytes an attribute of this format occupies, for the overlap check below.
    uint32_t formatSize(vk::Format format)
    {
        switch (format)
        {
            case vk::Format::eR32Sfloat:
            case vk::Format::eR32Uint:
                return 4;
            case vk::Format::eR32G32Sfloat:
            case vk::Format::eR32G32Uint:
                return 8;
            case vk::Format::eR32G32B32A32Sfloat:
            case vk::Format::eR32G32B32A32Uint:
                return 16;
            default:
                return 0;
        }
    }

    template <typename AttrArray>
    void checkAttributeTable(const AttrArray& attrs, uint32_t stride, uint32_t firstLocation)
    {
        for (size_t i = 0; i < attrs.size(); ++i)
        {
            CAPTURE(i);
            CHECK(static_cast<uint32_t>(attrs[i].binding) == 1u);
            CHECK(static_cast<uint32_t>(attrs[i].location) ==
                  firstLocation + static_cast<uint32_t>(i));

            const uint32_t size = formatSize(attrs[i].format);
            CHECK_MESSAGE(size != 0, "unhandled vk::Format in the text instance layout");
            // Reading past the stride is undefined without the portability-subset
            // vertexAttributeAccessBeyondStride feature.
            CHECK(attrs[i].offset + size <= stride);

            if (i > 0)
            {
                const uint32_t prevEnd = attrs[i - 1].offset + formatSize(attrs[i - 1].format);
                CHECK(attrs[i].offset >= prevEnd);
            }
        }
    }
}

TEST_SUITE("TextInstanceLayout")
{
    TEST_CASE("TextCharInstance keeps its packed 120-byte layout")
    {
        using render::text::TextCharInstance;

        CHECK(sizeof(TextCharInstance) == 120);
        CHECK(offsetof(TextCharInstance, effectParams) == 84);
        CHECK(offsetof(TextCharInstance, effectColors) == 100);
        CHECK(offsetof(TextCharInstance, effectMargin) == 116);

        // The effect block sits strictly after everything VK-1635 did not touch, so the
        // pre-existing attributes keep the offsets the shipped shader already expects.
        CHECK(offsetof(TextCharInstance, effectParams) >
              offsetof(TextCharInstance, styleFlags));
    }

    TEST_CASE("TextCharInstance attributes are monotonic, non-overlapping and in-stride")
    {
        using render::text::TextCharInstance;

        const auto attrs = TextCharInstance::getAttributeDescriptions();
        REQUIRE(attrs.size() == 10);

        const auto binding = TextCharInstance::getBindingDescription();
        CHECK(binding.stride == sizeof(TextCharInstance));
        CHECK(binding.inputRate == vk::VertexInputRate::eInstance);

        // text.glsl uses locations 0..1 for the shared quad vertex, so instance data
        // starts at 2 and the VK-1635 block lands on 9..11.
        checkAttributeTable(attrs, binding.stride, 2u);

        CHECK(attrs[7].offset == offsetof(TextCharInstance, effectParams));
        CHECK(attrs[8].offset == offsetof(TextCharInstance, effectColors));
        CHECK(attrs[9].offset == offsetof(TextCharInstance, effectMargin));
        CHECK(attrs[9].location == 11u);

        // effectColors is read as uvec4 in GLSL: a float format here would silently
        // reinterpret the packed colours as denormals.
        CHECK(attrs[8].format == vk::Format::eR32G32B32A32Uint);
    }

    TEST_CASE("UITextCharInstance keeps its packed 96-byte layout")
    {
        using render::ui::UITextCharInstance;

        CHECK(sizeof(UITextCharInstance) == 96);
        CHECK(offsetof(UITextCharInstance, effectParams) == 60);
        CHECK(offsetof(UITextCharInstance, effectColors) == 76);
        CHECK(offsetof(UITextCharInstance, effectMargin) == 92);
    }

    TEST_CASE("UITextCharInstance attributes are monotonic, non-overlapping and in-stride")
    {
        using render::ui::UITextCharInstance;

        const auto attrs = UITextCharInstance::getAttributeDescriptions();
        REQUIRE(attrs.size() == 8);

        const auto binding = UITextCharInstance::getBindingDescription();
        CHECK(binding.stride == sizeof(UITextCharInstance));
        CHECK(binding.inputRate == vk::VertexInputRate::eInstance);

        // ui_text.glsl carries no world position / renderMode / entityId, so its instance
        // block is shorter and the VK-1635 attributes land on 7..9.
        checkAttributeTable(attrs, binding.stride, 2u);

        CHECK(attrs[5].offset == offsetof(UITextCharInstance, effectParams));
        CHECK(attrs[6].offset == offsetof(UITextCharInstance, effectColors));
        CHECK(attrs[7].offset == offsetof(UITextCharInstance, effectMargin));
        CHECK(attrs[7].location == 9u);
        CHECK(attrs[6].format == vk::Format::eR32G32B32A32Uint);
    }

    TEST_CASE("VK-1635 did not touch the text push constants")
    {
        // The effect data is per-label, so it had to be per-instance; the 16-byte push
        // block was already full. test_text_push_constants.cpp owns the detail - this is
        // the reminder that growing the block was considered and rejected.
        CHECK(sizeof(render::text::TextPushConstants) == 16);
        CHECK(sizeof(render::ui::UITextPushConstants) == 16);
    }
}
