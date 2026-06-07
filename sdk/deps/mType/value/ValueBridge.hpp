#pragma once
//
// ValueBridge — MYT-126 SPIKE (flag-on only).
//
// Bridge infrastructure that lets the 16-byte tagged Value hold any of the
// 8 heap-typed shared_ptr alternatives plus std::string / InternedString
// without inflating the Value footprint.
//
// Every heap Value carries a single pointer to a RefCounted bridge. The
// bridge holds the actual shared_ptr<T> (or inline string) and carries a
// Kind tag so the shim can disambiguate between, e.g., NativeArray vs.
// FlatMultiArray under the same ValueType::ARRAY tag on Value.
//
// Cost: each heap Value creation pays one extra allocation for the bridge.
// Acceptable for the SPIKE — the measured win is Value-footprint shrink and
// variant-access elimination, not fewer allocations.
//

#include "IntrusivePtr.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace value
{
    enum class BridgeKind : uint8_t
    {
        STD_STRING,
        INTERNED_STRING,
        OBJECT_INSTANCE,
        VALUE_OBJECT,
        BYTECODE_LAMBDA,
        NATIVE_ARRAY,
        FLAT_MULTI_ARRAY,
        SPARSE_MULTI_ARRAY,
        FLAT_MULTI_OBJECT_ARRAY,
        PROMISE
    };

    class BridgeBase : public RefCounted
    {
    public:
        explicit BridgeBase(BridgeKind k) noexcept : kind_(k) {}
        BridgeKind kind() const noexcept { return kind_; }

    protected:
        // MYT-317: override the RefCounted destruction terminal so dead
        // bridges return to BridgeArena instead of going straight to
        // operator delete. Definition lives in ValueTypeUtils.cpp to keep
        // BridgeArena out of this header (avoids a cycle).
        void destroy() noexcept override;

    private:
        BridgeKind kind_;
    };

    template <BridgeKind K, typename Held>
    class TypedBridge final : public BridgeBase
    {
    public:
        explicit TypedBridge(Held h) : BridgeBase(K), held_(std::move(h)) {}
        const Held& get() const noexcept { return held_; }
        Held& get() noexcept { return held_; }

        // Byte offset of held_ within the bridge. Consumed by the JIT emitter
        // to synthesize inline shared_ptr dereferencing without a helper call
        // (MYT-169 / MYT-190 Phase 2). Not constexpr: offsetof on a
        // polymorphic type is conditionally-supported.
        static size_t heldOffset() noexcept
        {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4413)
#endif
            return offsetof(TypedBridge, held_);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        }

    private:
        Held held_;
    };
}
