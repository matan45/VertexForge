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
    private:
        std::vector<PendingReference> pendingRefs;
        std::vector<PendingReference> resolvedRefs;
    public:
        void addPendingReference(uint64_t sourceUUID, uint64_t targetUUID, ReferenceType type);

        void onSectorLoaded(const std::vector<uint64_t>& loadedUUIDs);
        void onSectorUnloaded(const std::vector<uint64_t>& unloadedUUIDs);

        [[nodiscard]] const std::vector<PendingReference>& getResolved() const { return resolvedRefs; }
        void clearResolved() { resolvedRefs.clear(); }

        [[nodiscard]] size_t pendingCount() const { return pendingRefs.size(); }
        [[nodiscard]] size_t resolvedCount() const { return resolvedRefs.size(); }

   
    };

} // namespace world
