#include <doctest.h>
#include <registry/ImporterRegistry.hpp>
#include <registry/builtin/BuiltinImporters.hpp>
#include <controllers/Import.hpp>
#include <controllers/files/FileUtils.hpp>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// ImporterRegistry tests (Import.dll)
//
// These pin the detection behavior the registry inherited from the
// pre-registry FileTypeDetectionStage if-chain: exact signature bytes,
// priority order, and the fileType -> output extension / asset type tables.
// ============================================================

namespace
{
    constexpr size_t headerSize = 32; // HeaderReadingStage::HEADER_SIZE

    std::vector<unsigned char> headerFromBytes(std::initializer_list<unsigned char> bytes)
    {
        std::vector<unsigned char> header(bytes);
        header.resize(headerSize, 0);
        return header;
    }

    std::vector<unsigned char> headerFromText(const char* text)
    {
        std::vector<unsigned char> header(text, text + std::strlen(text));
        header.resize(headerSize, 0);
        return header;
    }

    std::string detect(const std::vector<unsigned char>& header, uint64_t fileSize = 0,
                       std::string_view path = "")
    {
        import::builtin::ensureRegistered();
        const import::DetectionInput input{path, std::span<const unsigned char>(header), fileSize};
        return import::ImporterRegistry::instance().detect(input);
    }

    std::vector<unsigned char> tgaHeader()
    {
        std::vector<unsigned char> header(headerSize, 0);
        header[1] = 0;   // colorMapType
        header[2] = 2;   // uncompressed truecolor
        header[12] = 16; // width LE
        header[14] = 16; // height LE
        header[16] = 32; // pixel depth
        return header;
    }
}

TEST_SUITE("ImporterRegistry")
{
    TEST_CASE("signature formats detect exactly as before")
    {
        CHECK(detect(headerFromBytes({0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A})) == "PNG");
        CHECK(detect(headerFromBytes({0xFF, 0xD8, 0xFF, 0xE0})) == "JPEG");
        CHECK(detect(headerFromBytes({0x42, 0x4D, 0x36, 0x00})) == "BMP");
        CHECK(detect(headerFromText("OggS")) == "OGG");
        CHECK(detect(headerFromText("glTF")) == "GLB");
        CHECK(detect(headerFromText("OTTO")) == "OTF");
        CHECK(detect(headerFromBytes({0x76, 0x2F, 0x31, 0x01})) == "EXR");
    }

    TEST_CASE("heuristic formats detect exactly as before")
    {
        CHECK(detect(headerFromText("#?RADIANCE")) == "HDR");
        CHECK(detect(headerFromText("#?RGBE")) == "HDR");
        CHECK(detect(headerFromText("ID3")) == "MP3");
        CHECK(detect(headerFromBytes({0xFF, 0xFB, 0x90, 0x00})) == "MP3"); // frame sync
        CHECK(detect(headerFromText("RIFF\x01\x02\x03\x04WAVE")) == "WAV");
        CHECK(detect(headerFromText("Kaydara FBX Binary  ")) == "FBX");
        CHECK(detect(headerFromText("<?xml v=1><COLLADA>")) == "DAE");
        CHECK(detect(headerFromText("{\"asset\":{\"v\":2}}")) == "GLTF");
        CHECK(detect(headerFromText("v 1.0 2.0 3.0")) == "OBJ");
        CHECK(detect(headerFromBytes({0x00, 0x01, 0x00, 0x00})) == "TTF");
        CHECK(detect(headerFromText("true")) == "TTF");
        CHECK(detect(headerFromText("ttcf")) == "TTF");
        CHECK(detect(tgaHeader()) == "TGA");
    }

    TEST_CASE("unmatched header is Unknown")
    {
        std::vector<unsigned char> garbage(headerSize, 0xEE);
        CHECK(detect(garbage) == "Unknown");
        CHECK(detect({}) == "Unknown");
    }

    TEST_CASE("priority order: DAE wins over GLTF-like and OBJ-like content")
    {
        // A COLLADA header also contains "v " style sequences; DAE (40) must
        // beat OBJ (20).
        CHECK(detect(headerFromText("<?xml ?><COLLADA v ")) == "DAE");
        // GLTF (30) must beat OBJ (20) even when OBJ keywords appear inside.
        CHECK(detect(headerFromText("{\"asset\": \"v 1\"}")) == "GLTF");
    }

    TEST_CASE("format table: output extension and asset type per fileType")
    {
        import::builtin::ensureRegistered();
        auto& registry = import::ImporterRegistry::instance();

        const struct { const char* fileType; const char* ext; resource::AssetType type; } expected[] = {
            {"PNG", "vfImage", resource::AssetType::Texture},
            {"JPEG", "vfImage", resource::AssetType::Texture},
            {"BMP", "vfImage", resource::AssetType::Texture},
            {"TGA", "vfImage", resource::AssetType::Texture},
            {"HDR", "vfHdr", resource::AssetType::HDR},
            {"EXR", "vfHdr", resource::AssetType::HDR},
            {"MP3", "vfAudio", resource::AssetType::Audio},
            {"WAV", "vfAudio", resource::AssetType::Audio},
            {"OGG", "vfAudio", resource::AssetType::Audio},
            {"OBJ", "vfMesh", resource::AssetType::Mesh},
            {"FBX", "vfMesh", resource::AssetType::Mesh},
            {"DAE", "vfMesh", resource::AssetType::Mesh},
            {"GLTF", "vfMesh", resource::AssetType::Mesh},
            {"GLB", "vfMesh", resource::AssetType::Mesh},
            {"TTF", "vfFont", resource::AssetType::Font},
            {"OTF", "vfFont", resource::AssetType::Font},
        };

        for (const auto& e : expected)
        {
            CAPTURE(e.fileType);
            auto info = registry.formatInfo(e.fileType);
            REQUIRE(info.has_value());
            CHECK(info->outputExtension == e.ext);
            CHECK(info->assetType == e.type);
            CHECK(registry.importerFor(e.fileType) != nullptr);
            CHECK(controllers::Import::assetTypeFor(e.fileType) == e.type);
        }

        CHECK_FALSE(registry.formatInfo("Unknown").has_value());
        CHECK(registry.importerFor("Unknown") == nullptr);
        CHECK(controllers::Import::assetTypeFor("Unknown") == resource::AssetType::COUNT);
    }

    TEST_CASE("supportedFormats covers all builtin extensions")
    {
        auto formats = controllers::Import::supportedFormats();

        auto hasExtension = [&](const char* ext)
        {
            for (const auto& info : formats)
                for (const auto& e : info.extensions)
                    if (e == ext) return true;
            return false;
        };

        for (const char* ext : {"png", "jpg", "jpeg", "bmp", "tga", "hdr", "exr",
                                "mp3", "wav", "ogg", "obj", "fbx", "dae", "gltf", "glb",
                                "ttf", "otf"})
        {
            CAPTURE(ext);
            CHECK(hasExtension(ext));
        }
    }

    TEST_CASE("FileUtils category checks match the registry")
    {
        CHECK(files::FileUtils::isTextureFile("a.png"));
        CHECK(files::FileUtils::isTextureFile("a.JPG"));
        CHECK(files::FileUtils::isTextureFile("a.jpeg"));
        CHECK(files::FileUtils::isTextureFile("a.bmp"));
        CHECK(files::FileUtils::isTextureFile("a.tga"));
        CHECK_FALSE(files::FileUtils::isTextureFile("a.hdr"));

        CHECK(files::FileUtils::isHDRFile("a.hdr"));
        CHECK(files::FileUtils::isHDRFile("a.exr"));
        CHECK_FALSE(files::FileUtils::isHDRFile("a.png"));

        CHECK(files::FileUtils::isMeshFile("a.obj"));
        CHECK(files::FileUtils::isMeshFile("a.fbx"));
        CHECK(files::FileUtils::isMeshFile("a.dae"));
        CHECK(files::FileUtils::isMeshFile("a.gltf"));
        CHECK(files::FileUtils::isMeshFile("a.glb"));
        CHECK_FALSE(files::FileUtils::isMeshFile("a.wav"));

        CHECK(files::FileUtils::isAudioFile("a.mp3"));
        CHECK(files::FileUtils::isAudioFile("a.wav"));
        CHECK(files::FileUtils::isAudioFile("a.ogg"));
        CHECK_FALSE(files::FileUtils::isAudioFile("a.obj"));
    }

    TEST_CASE("higher-priority registration overrides a builtin, unregister restores it")
    {
        class FakePngImporter : public import::AssetImporter
        {
        public:
            std::vector<import::FormatInfo> formats() const override
            {
                return {{"FAKEPNG", "Fake Files", {"png"}, "vfFake", resource::AssetType::COUNT, 200}};
            }

            bool matches(const std::string& fileType, const import::DetectionInput& input) const override
            {
                return fileType == "FAKEPNG" && input.header.size() >= 2 &&
                       input.header[0] == 0x89 && input.header[1] == 0x50;
            }

            void process(pipeline::ImportContext&) override {}
        };

        import::builtin::ensureRegistered();
        auto& registry = import::ImporterRegistry::instance();
        auto pngHeader = headerFromBytes({0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});

        registry.registerImporter(std::make_unique<FakePngImporter>(), "test_import_registry");
        CHECK(registry.hasOwner("test_import_registry"));
        CHECK(detect(pngHeader) == "FAKEPNG");
        CHECK(registry.formatInfo("FAKEPNG").has_value());

        registry.unregisterByOwner("test_import_registry");
        CHECK_FALSE(registry.hasOwner("test_import_registry"));
        CHECK(detect(pngHeader) == "PNG");
        CHECK_FALSE(registry.formatInfo("FAKEPNG").has_value());
    }

    TEST_CASE("importer-declared options surface per extension")
    {
        class OptionedImporter : public import::AssetImporter
        {
        public:
            std::vector<import::FormatInfo> formats() const override
            {
                return {{"OPTFMT", "Opt Files", {"optfmt"}, "vfImage", resource::AssetType::Texture, 1}};
            }

            bool matches(const std::string&, const import::DetectionInput&) const override
            {
                return false;
            }

            void process(pipeline::ImportContext&) override {}

            std::vector<import::ImportOptionDesc> options() const override
            {
                import::ImportOptionDesc flip;
                flip.key = "flipVertically";
                flip.label = "Flip Vertically";
                flip.type = import::ImportOptionDesc::Type::Bool;
                flip.defaultValue = true;

                import::ImportOptionDesc scale;
                scale.key = "scale";
                scale.label = "Scale";
                scale.type = import::ImportOptionDesc::Type::Float;
                scale.defaultValue = 1.0f;
                scale.minValue = 0.1f;
                scale.maxValue = 10.0f;

                return {flip, scale};
            }
        };

        import::builtin::ensureRegistered();
        auto& registry = import::ImporterRegistry::instance();
        registry.registerImporter(std::make_unique<OptionedImporter>(), "test_options");

        auto options = registry.optionsForExtension("optfmt");
        REQUIRE(options.size() == 2);
        CHECK(options[0].key == "flipVertically");
        CHECK(std::get<bool>(options[0].defaultValue) == true);
        CHECK(options[1].key == "scale");
        CHECK(std::get<float>(options[1].defaultValue) == doctest::Approx(1.0f));

        // Builtin formats declare no options today.
        CHECK(registry.optionsForExtension("png").empty());

        // Values round-trip through ImportConfig::customOptions.
        importConfig::ImportConfig config;
        config.customOptions["flipVertically"] = false;
        config.customOptions["scale"] = 2.5f;
        CHECK(std::get<bool>(config.customOptions.at("flipVertically")) == false);
        CHECK(std::get<float>(config.customOptions.at("scale")) == doctest::Approx(2.5f));

        registry.unregisterByOwner("test_options");
        CHECK(registry.optionsForExtension("optfmt").empty());
    }

    TEST_CASE("animation-only mesh import redirects output to .vfAnim")
    {
        import::builtin::ensureRegistered();
        auto* importer = import::ImporterRegistry::instance().importerFor("FBX");
        REQUIRE(importer != nullptr);

        importConfig::ImportConfig config;
        pipeline::ImportContext context(importConfig::ImportFiles("anim.fbx", config), "out");
        context.fileName = "anim";
        context.fileType = "FBX";

        // Default: standard mesh output and the format's asset type.
        CHECK(importer->deriveOutputFile(context).empty());
        CHECK(importer->deriveAssetType(context) == resource::AssetType::COUNT);

        context.file.config.customOptions["animationOnly"] = true;
        CHECK(importer->deriveOutputFile(context) == "anim.vfAnim");
        CHECK(importer->deriveAssetType(context) == resource::AssetType::Animation);

        // The option surfaces in the import dialog for mesh extensions.
        auto options = import::ImporterRegistry::instance().optionsForExtension("fbx");
        REQUIRE(options.size() == 1);
        CHECK(options[0].key == "animationOnly");
    }

    TEST_CASE("parallel detection is stable")
    {
        import::builtin::ensureRegistered();

        const std::vector<std::pair<std::vector<unsigned char>, std::string>> cases = {
            {headerFromBytes({0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}), "PNG"},
            {headerFromText("OggS"), "OGG"},
            {headerFromText("Kaydara FBX Binary"), "FBX"},
            {tgaHeader(), "TGA"},
            {std::vector<unsigned char>(headerSize, 0xEE), "Unknown"},
        };

        std::atomic<int> failures{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
        {
            threads.emplace_back([&]()
            {
                for (int i = 0; i < 500; ++i)
                {
                    for (const auto& [header, expected] : cases)
                    {
                        if (detect(header) != expected)
                            failures.fetch_add(1);
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();

        CHECK(failures.load() == 0);
    }
}
