#pragma once
#include <span>
#include "../../value/ValueType.hpp"
#include "../NativeContext.hpp"

namespace environment::registry
{
    /**
     * Signature for native functions: (userData, Context, Arguments)
     */
    using NativeFunctionSignature = ::value::Value (*)(void* userData, ::environment::NativeContext& ctx, std::span<const ::value::Value> args);

    /**
     * Lightweight delegate for native function calls.
     * Replaces std::function to avoid heap allocations and virtual dispatch overhead in hot loops.
     */
    struct NativeDelegate {
        void* userData = nullptr;
        NativeFunctionSignature invoke = nullptr;

        ::value::Value operator()(::environment::NativeContext& ctx, std::span<const ::value::Value> args) const {
            return invoke(userData, ctx, args);
        }

        explicit operator bool() const { return invoke != nullptr; }
        bool operator==(const NativeDelegate& other) const {
            return userData == other.userData && invoke == other.invoke;
        }
    };
}
