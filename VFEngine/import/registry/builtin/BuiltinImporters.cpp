#include "BuiltinImporters.hpp"
#include "../ImporterRegistry.hpp"
#include "TextureImporter.hpp"
#include "HdrImporter.hpp"
#include "KtxImporter.hpp"
#include "DdsImporter.hpp"
#include "AudioImporter.hpp"
#include "MeshImporter.hpp"
#include "FontImporter.hpp"
#include <mutex>

namespace import::builtin
{
    void ensureRegistered()
    {
        static std::once_flag registeredFlag;
        std::call_once(registeredFlag, []()
        {
            auto& registry = ImporterRegistry::instance();
            // Registration order is the tie-breaker for equal detection
            // priorities; it mirrors the pre-registry detection order.
            registry.registerImporter(std::make_unique<TextureImporter>());
            registry.registerImporter(std::make_unique<HdrImporter>());
            registry.registerImporter(std::make_unique<KtxImporter>()); // KTX2/KTX1 (VK-1642)
            registry.registerImporter(std::make_unique<DdsImporter>()); // DDS (VK-1642)
            registry.registerImporter(std::make_unique<AudioImporter>());
            registry.registerImporter(std::make_unique<MeshImporter>());
            registry.registerImporter(std::make_unique<FontImporter>());
        });
    }
}
