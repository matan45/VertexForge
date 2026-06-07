#pragma once
#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <cassert>
#include <limits>
#include "StringPool.hpp"
#include "ValueBridge.hpp"

namespace runtimeTypes::klass
{
    class ObjectInstance;
}

namespace vm::runtime
{
    struct BytecodeLambda;
}

namespace value
{
    class NativeArray;
    class FlatMultiArray;
    class SparseMultiArray;
    class PromiseValue;
    class ValueObject;
}

namespace mType::value::arrays
{
    class FlatMultiObjectArray;
}


namespace value
{
    // Value types that the language supports
    enum class ValueType :uint8_t
    {
        INT,
        FLOAT,
        BOOL,
        STRING,
        VOID,
        OBJECT,
        VALUE_OBJECT,
        ARRAY,
        LAMBDA,
        NULL_TYPE,
        PROMISE,
        // MYT-134: non-owning borrowed raw ObjectInstance*. Lifetime is owned by
        // CallFrame::stackObjects (released at frame teardown). No refcount ops;
        // not a heap tag. Never serialized to .mtc — only produced by NEW_STACK
        // at runtime.
        STACK_OBJECT,
        // Inline resolved-Promise variant. Represents Promise<Int> already
        // resolved with an int value. Payload holds the raw int inline; no
        // heap PromiseValue is allocated. AWAIT unwraps to a plain INT, which
        // INVOKE_INT_GET_VALUE and arithmetic executors handle natively.
        //
        // Note: .then()/.catch_() are not surfaced to user code today — the
        // only callers are inside VirtualMachineAsync.cpp on an already-
        // narrowed AsyncPromiseValue. If/when those methods are ever exposed
        // to .mt programs, an inline PROMISE_INT receiver must first be
        // materialized to a real AsyncPromiseValue (slow path). That helper
        // does not yet exist; add it before exposing the methods.
        PROMISE_INT,
        // MYT-317: short-string optimization. Up to 14 bytes of UTF-8 content
        // live inline in the Value (offsets 2..15) with the length at offset 1.
        // Not a heap tag — refcount ops skip it. Cross-equality with STRING
        // (heap STD_STRING / INTERNED_STRING) is handled in Value::operator==.
        STRING_INLINE
    };

    //
    // 16-byte tagged Value (MYT-126 go).
    //
    // Layout: 8-byte payload + 1-byte tag + 7 bytes padding (sizeof == 16).
    // Primitives (INT/FLOAT/BOOL/VOID/NULL_TYPE) live inline in the payload.
    // Everything else routes through a RefCounted BridgeBase* — see ValueBridge.hpp.
    //
    // Heap-type constructors are declared here but defined out-of-line in
    // ValueTypeUtils.cpp so this header doesn't need to pull in the full heap
    // type definitions.
    //
    class Value final
    {
    public:
        Value() noexcept : tag_(ValueType::VOID) { payload_.i = 0; }

        Value(int64_t v) noexcept : tag_(ValueType::INT) { payload_.i = v; }
        Value(int v) noexcept : tag_(ValueType::INT) { payload_.i = v; }
        Value(unsigned int v) noexcept : tag_(ValueType::INT) { payload_.i = v; }
        Value(size_t v) noexcept : tag_(ValueType::INT)
        {
            assert(v <= static_cast<size_t>((std::numeric_limits<int64_t>::max)())
                   && "Value(size_t): truncation to int64_t");
            payload_.i = static_cast<int64_t>(v);
        }
        Value(double v) noexcept : tag_(ValueType::FLOAT) { payload_.d = v; }
        Value(float v) noexcept : tag_(ValueType::FLOAT) { payload_.d = v; }
        Value(bool v) noexcept : tag_(ValueType::BOOL) { payload_.b = v; }
        Value(std::nullptr_t) noexcept : tag_(ValueType::NULL_TYPE) { payload_.i = 0; }
        Value(std::monostate) noexcept : tag_(ValueType::VOID) { payload_.i = 0; }

        // MYT-134: non-owning borrowed ObjectInstance*. Tag struct disambiguates
        // from the shared_ptr<ObjectInstance> ctor below.
        struct StackObjectTag {};
        Value(runtimeTypes::klass::ObjectInstance* raw, StackObjectTag) noexcept
            : tag_(ValueType::STACK_OBJECT)
        {
            payload_.stackObject = raw;
        }

        // Inline resolved-Promise variant. Disambiguated via tag struct; the
        // int lives directly in payload_.i with tag PROMISE_INT.
        struct PromiseIntTag {};
        Value(int64_t v, PromiseIntTag) noexcept : tag_(ValueType::PROMISE_INT)
        {
            payload_.i = v;
        }

        // MYT-317: short-string optimization. Mandatory tag struct disambiguates
        // from Value(const char*) (which bridges) and prevents the
        // pointer→Value(bool) decay regression — see
        // [[feedback_value_bool_pointer_decay]]. `len` must be <= 14.
        // The init list activates the inline-overlay struct (inlineTag_ /
        // inlineLen_ / inlineChars_) so all subsequent writes stay within the
        // active union member.
        struct InlineStringTag {};
        static constexpr uint8_t INLINE_STRING_CAP = 14;
        Value(InlineStringTag, const char* data, uint8_t len) noexcept
        {
            assert(len <= INLINE_STRING_CAP && "InlineStringTag: len must be <= 14");
            inlineTag_ = ValueType::STRING_INLINE;
            inlineLen_ = len;
            if (len) std::memcpy(inlineChars_, data, len);
        }

        // Implicit ctors from each shared_ptr heap type + string types.
        // Each wraps the argument in a TypedBridge<Kind, Held> and stores
        // the bridge pointer. Defined in ValueType.cpp.
        Value(const std::shared_ptr<runtimeTypes::klass::ObjectInstance>& p);
        Value(std::shared_ptr<runtimeTypes::klass::ObjectInstance>&& p);
        Value(const std::shared_ptr<ValueObject>& p);
        Value(std::shared_ptr<ValueObject>&& p);
        Value(const std::shared_ptr<NativeArray>& p);
        Value(std::shared_ptr<NativeArray>&& p);
        Value(const std::shared_ptr<FlatMultiArray>& p);
        Value(std::shared_ptr<FlatMultiArray>&& p);
        Value(const std::shared_ptr<SparseMultiArray>& p);
        Value(std::shared_ptr<SparseMultiArray>&& p);
        Value(const std::shared_ptr<mType::value::arrays::FlatMultiObjectArray>& p);
        Value(std::shared_ptr<mType::value::arrays::FlatMultiObjectArray>&& p);
        Value(const std::shared_ptr<vm::runtime::BytecodeLambda>& p);
        Value(std::shared_ptr<vm::runtime::BytecodeLambda>&& p);
        Value(const std::shared_ptr<PromiseValue>& p);
        Value(std::shared_ptr<PromiseValue>&& p);
        Value(const std::string& s);
        Value(std::string&& s);
        Value(const char* s);
        Value(const InternedString& s);
        Value(InternedString&& s);

        // MYT-317: copies via raw byte-copy of the full 16B storage because
        // STRING_INLINE content occupies what used to be tag padding (offsets
        // 1..7). Memberwise `tag_/payload_` copy would skip those bytes.
        // Value has no virtual functions and trivially-bit-copyable storage
        // (the 8-byte payload is itself a POD union of value types and a raw
        // pointer), so memcpy is safe — only the refcount needs explicit
        // adjustment via retainIfHeap/releaseIfHeap.
        Value(const Value& other) noexcept
        {
            std::memcpy(this, &other, sizeof(Value));
            retainIfHeap();
        }

        Value(Value&& other) noexcept
        {
            std::memcpy(this, &other, sizeof(Value));
            other.tag_ = ValueType::VOID;
            other.payload_.i = 0;
        }

        Value& operator=(const Value& other) noexcept
        {
            if (this != &other)
            {
                releaseIfHeap();
                std::memcpy(this, &other, sizeof(Value));
                retainIfHeap();
            }
            return *this;
        }

        Value& operator=(Value&& other) noexcept
        {
            if (this != &other)
            {
                releaseIfHeap();
                std::memcpy(this, &other, sizeof(Value));
                other.tag_ = ValueType::VOID;
                other.payload_.i = 0;
            }
            return *this;
        }

        ~Value() noexcept { releaseIfHeap(); }

        ValueType tag() const noexcept { return tag_; }

        // Raw payload accessors — used only by ValueShim free functions.
        int64_t rawInt() const noexcept { return payload_.i; }
        double rawFloat() const noexcept { return payload_.d; }
        bool rawBool() const noexcept { return payload_.b; }
        BridgeBase* rawBridge() const noexcept { return static_cast<BridgeBase*>(payload_.ptr); }
        // MYT-134: raw pointer accessor for STACK_OBJECT values. Undefined unless tag_ == STACK_OBJECT.
        runtimeTypes::klass::ObjectInstance* rawStackObject() const noexcept { return payload_.stackObject; }
        // MYT-317: STRING_INLINE accessors. Undefined unless tag_ == STRING_INLINE.
        const char* rawInlineChars() const noexcept { return inlineChars_; }
        uint8_t rawInlineLen() const noexcept { return inlineLen_; }

        // Byte offset of payload_ within Value. Consumed by the JIT emitter
        // to synthesize inline loads of the bridge pointer without calling a
        // helper. Not constexpr because offsetof on a non-standard-layout
        // type is conditionally-supported; same pattern as
        // ObjectInstance::classDefinitionMemberOffset.
        static size_t payloadOffset() noexcept;

        // Comparison operators. Matches flag-off std::variant::operator==
        // semantics: primitives by value, STRING by content (both STD_STRING
        // and INTERNED_STRING kinds, cross-kind falls back to content), other
        // heap kinds by held shared_ptr::get() (the shared_ptr default ==).
        bool operator==(const Value& other) const noexcept
        {
            // MYT-317: cross-kind string equality between STRING and STRING_INLINE.
            const bool lhsStr = (tag_ == ValueType::STRING || tag_ == ValueType::STRING_INLINE);
            const bool rhsStr = (other.tag_ == ValueType::STRING || other.tag_ == ValueType::STRING_INLINE);
            if (lhsStr && rhsStr)
            {
                return stringEquals(other);
            }
            if (tag_ != other.tag_) return false;
            switch (tag_)
            {
            case ValueType::INT:          return payload_.i == other.payload_.i;
            case ValueType::FLOAT:        return payload_.d == other.payload_.d;
            case ValueType::BOOL:         return payload_.b == other.payload_.b;
            case ValueType::VOID:
            case ValueType::NULL_TYPE:    return true;
            case ValueType::STACK_OBJECT: return payload_.stackObject == other.payload_.stackObject;
            case ValueType::PROMISE_INT:  return payload_.i == other.payload_.i;
            default:                      return equalsHeap(other);
            }
        }
        bool operator!=(const Value& other) const noexcept { return !(*this == other); }

    private:
        bool equalsHeap(const Value& other) const noexcept;
        // MYT-317: STRING / STRING_INLINE cross-kind content equality.
        // Out-of-line in ValueTypeUtils.cpp so the bridge-kind dispatch
        // can use the StringPool / InternedString full types.
        bool stringEquals(const Value& other) const noexcept;

        union Payload
        {
            int64_t i;
            double d;
            bool b;
            RefCounted* ptr;
            // MYT-134: non-owning raw pointer. Set only when tag_ == STACK_OBJECT.
            // Not reference-counted — CallFrame owns the lifetime.
            runtimeTypes::klass::ObjectInstance* stackObject;
        };

        // MYT-317: anonymous union overlays the heap form (tag + payload at
        // offset 8) with the inline-string form (tag + length + 14 bytes of
        // content). Both inner structs are 16B; the union's alignment is 8
        // (inherited from Payload). Bytes 1..7 are tag padding in the heap
        // form and the first half of the inline buffer in the inline form.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
        union {
            struct {
                ValueType tag_;
                Payload   payload_;
            };
            struct {
                ValueType inlineTag_;
                uint8_t   inlineLen_;
                char      inlineChars_[14];
            };
        };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

        bool isHeapTag() const noexcept
        {
            switch (tag_)
            {
            case ValueType::STRING:
            case ValueType::OBJECT:
            case ValueType::VALUE_OBJECT:
            case ValueType::ARRAY:
            case ValueType::LAMBDA:
            case ValueType::PROMISE:
                return true;
            default:
                return false;
            }
        }

        void retainIfHeap() noexcept
        {
            if (isHeapTag() && payload_.ptr) payload_.ptr->retain();
        }

        void releaseIfHeap() noexcept
        {
            if (isHeapTag() && payload_.ptr) payload_.ptr->release();
        }
    };

    static_assert(sizeof(Value) == 16,
                  "Tagged Value must be 16 bytes — adjust payload/padding");
    static_assert(alignof(Value) == 8,
                  "Tagged Value must be 8-byte aligned (JIT helper assumption)");

    // Primitive shim accessors — defined inline in ValueType.hpp so that
    // headers which only include ValueType.hpp (e.g. NativeArray.hpp) can
    // use them without pulling in ValueShim.hpp (which depends on heap types).
    // Heap-type accessors (isObject / asObject / etc.) live in ValueShim.hpp.
    // Template helpers for generic code (e.g. PrimitiveArray<T>::set).
    // holdsT<T> returns whether Value holds a T; getT<T> extracts it.
    // Only primitive Ts (int64_t/double/bool) are supported — instantiating
    // with a heap type (e.g. shared_ptr<ObjectInstance>) fires the
    // static_assert at compile time instead of silently returning false / T{}.
    namespace detail {
        template <typename> inline constexpr bool dependent_false_v = false;
    }
    template <typename T> inline bool holdsT(const Value& v) noexcept
    {
        if constexpr (std::is_same_v<T, int64_t>) return v.tag() == ValueType::INT;
        else if constexpr (std::is_same_v<T, double>) return v.tag() == ValueType::FLOAT;
        else if constexpr (std::is_same_v<T, bool>) return v.tag() == ValueType::BOOL;
        else
        {
            static_assert(detail::dependent_false_v<T>,
                          "holdsT<T>: only int64_t/double/bool supported; use isObject/asObject/etc. for heap types");
            return false;
        }
    }
    template <typename T> inline T getT(const Value& v) noexcept
    {
        if constexpr (std::is_same_v<T, int64_t>) return static_cast<T>(v.rawInt());
        else if constexpr (std::is_same_v<T, double>) return static_cast<T>(v.rawFloat());
        else if constexpr (std::is_same_v<T, bool>) return v.rawBool();
        else
        {
            static_assert(detail::dependent_false_v<T>,
                          "getT<T>: only int64_t/double/bool supported; use asObject/asString/etc. for heap types");
            return T{};
        }
    }

    inline bool isInt(const Value& v) noexcept { return v.tag() == ValueType::INT; }
    inline bool isFloat(const Value& v) noexcept { return v.tag() == ValueType::FLOAT; }
    inline bool isBool(const Value& v) noexcept { return v.tag() == ValueType::BOOL; }
    inline bool isVoid(const Value& v) noexcept { return v.tag() == ValueType::VOID; }
    inline bool isNullType(const Value& v) noexcept { return v.tag() == ValueType::NULL_TYPE; }
    inline bool isObject(const Value& v) noexcept { return v.tag() == ValueType::OBJECT; }
    inline bool isStackObject(const Value& v) noexcept { return v.tag() == ValueType::STACK_OBJECT; }
    // MYT-134: either OBJECT (shared_ptr via bridge) or STACK_OBJECT (raw borrowed). Use when the
    // executor treats both storage kinds uniformly (field get/set, method call).
    inline bool isAnyObject(const Value& v) noexcept
    {
        return v.tag() == ValueType::OBJECT || v.tag() == ValueType::STACK_OBJECT;
    }
    inline bool isValueObject(const Value& v) noexcept { return v.tag() == ValueType::VALUE_OBJECT; }
    inline bool isLambda(const Value& v) noexcept { return v.tag() == ValueType::LAMBDA; }
    inline bool isPromise(const Value& v) noexcept { return v.tag() == ValueType::PROMISE; }
    // Inline resolved-Promise<Int> form. Holds the resolved int in payload
    // with no heap allocation. Distinct from isPromise so that consumers
    // expecting a real heap PromiseValue (debugger, GC, callback machinery)
    // are unaffected — they only handle the strict PROMISE form.
    inline bool isPromiseInt(const Value& v) noexcept { return v.tag() == ValueType::PROMISE_INT; }
    // Either form. Use at AWAIT entry and at the async-return double-wrap
    // check, where heap and inline forms must be treated uniformly.
    inline bool isAnyPromise(const Value& v) noexcept
    {
        return v.tag() == ValueType::PROMISE || v.tag() == ValueType::PROMISE_INT;
    }
    // Extract the inline int. Undefined unless tag is PROMISE_INT.
    inline int64_t asPromiseIntValue(const Value& v) noexcept { return v.rawInt(); }
    inline bool isString(const Value& v) noexcept
    {
        return v.tag() == ValueType::STRING && v.rawBridge()->kind() == BridgeKind::STD_STRING;
    }
    inline bool isInternedString(const Value& v) noexcept
    {
        return v.tag() == ValueType::STRING && v.rawBridge()->kind() == BridgeKind::INTERNED_STRING;
    }
    // MYT-317: short-string optimization. tag-only check — no bridge.
    inline bool isInlineString(const Value& v) noexcept
    {
        return v.tag() == ValueType::STRING_INLINE;
    }
    // MYT-317: either form (heap STRING or STRING_INLINE). Use when a caller
    // treats both representations uniformly (printers, formatters, JSON
    // serial, string-content equality).
    inline bool isAnyString(const Value& v) noexcept
    {
        return v.tag() == ValueType::STRING || v.tag() == ValueType::STRING_INLINE;
    }
    inline bool isNativeArray(const Value& v) noexcept
    {
        return v.tag() == ValueType::ARRAY && v.rawBridge()->kind() == BridgeKind::NATIVE_ARRAY;
    }
    inline bool isFlatMultiArray(const Value& v) noexcept
    {
        return v.tag() == ValueType::ARRAY && v.rawBridge()->kind() == BridgeKind::FLAT_MULTI_ARRAY;
    }
    inline bool isSparseMultiArray(const Value& v) noexcept
    {
        return v.tag() == ValueType::ARRAY && v.rawBridge()->kind() == BridgeKind::SPARSE_MULTI_ARRAY;
    }
    inline bool isFlatMultiObjectArray(const Value& v) noexcept
    {
        return v.tag() == ValueType::ARRAY && v.rawBridge()->kind() == BridgeKind::FLAT_MULTI_OBJECT_ARRAY;
    }
    inline int64_t asInt(const Value& v) noexcept { return v.rawInt(); }
    inline double asFloat(const Value& v) noexcept { return v.rawFloat(); }
    inline bool asBool(const Value& v) noexcept { return v.rawBool(); }
    inline const std::string& asString(const Value& v)
    {
        assert(isString(v) && "asString(): tag/kind must be STRING/STD_STRING");
        using Bridge = TypedBridge<BridgeKind::STD_STRING, std::string>;
        return static_cast<Bridge*>(v.rawBridge())->get();
    }
    inline const InternedString& asInternedString(const Value& v)
    {
        assert(isInternedString(v) && "asInternedString(): tag/kind must be STRING/INTERNED_STRING");
        using Bridge = TypedBridge<BridgeKind::INTERNED_STRING, InternedString>;
        return static_cast<Bridge*>(v.rawBridge())->get();
    }
    // MYT-317: SSO-aware accessor. Returns a string_view that points into
    // either the inline buffer (STRING_INLINE) or the bridge-held storage
    // (STRING / STD_STRING / INTERNED_STRING). Use when the caller only
    // needs read access and can handle both representations.
    inline std::string_view asStringView(const Value& v) noexcept
    {
        if (v.tag() == ValueType::STRING_INLINE)
        {
            return std::string_view(v.rawInlineChars(), v.rawInlineLen());
        }
        auto* bridge = v.rawBridge();
        if (bridge->kind() == BridgeKind::STD_STRING)
        {
            using B = TypedBridge<BridgeKind::STD_STRING, std::string>;
            return static_cast<B*>(bridge)->get();
        }
        using B = TypedBridge<BridgeKind::INTERNED_STRING, InternedString>;
        return static_cast<B*>(bridge)->get().getString();
    }
}
