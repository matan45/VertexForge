#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace world
{
    enum class ReferenceType : uint8_t
    {
        Parent,
        SocketAttachment,
        IKTarget
    };

    struct PendingReference
    {
        uint64_t sourceUUID;
        uint64_t targetUUID;
        ReferenceType type;
    };

    class PendingReferenceResolver
    {
    public:
        void addPendingReference(uint64_t sourceUUID, uint64_t targetUUID, ReferenceType type);

        void onSectorLoaded(const std::vector<uint64_t>& loadedUUIDs);
        void onSectorUnloaded(const std::vector<uint64_t>& unloadedUUIDs);

        [[nodiscard]] size_t pendingCount() const { return pendingRefs.size(); }

    private:
        std::vector<PendingReference> pendingRefs;
        std::vector<PendingReference> resolvedRefs;
    };

} // namespace world
