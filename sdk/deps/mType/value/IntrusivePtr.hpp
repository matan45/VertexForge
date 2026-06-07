#pragma once
//
// IntrusivePtr<T> + RefCounted base.
//
// Non-atomic, intrusive refcount backing the tagged Value. The eight heap
// types inherit RefCounted; Value stores IntrusivePtr<RefCounted> in its
// 16-byte tagged payload.
//
// Non-atomic is deliberate: the interpreter is single-threaded inside the
// VM loop. If a bench shows contention on the async tier, revisit.
//

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace value
{
    class RefCounted
    {
    public:
        RefCounted() noexcept = default;
        virtual ~RefCounted() = default;

        RefCounted(const RefCounted&) = delete;
        RefCounted& operator=(const RefCounted&) = delete;
        RefCounted(RefCounted&&) = delete;
        RefCounted& operator=(RefCounted&&) = delete;

        void retain() noexcept { ++refcount_; }

        void release() noexcept
        {
            if (--refcount_ == 0)
            {
                destroy();
            }
        }

        uint32_t refcount() const noexcept { return refcount_; }

    protected:
        // MYT-317: terminal destruction hook. Default deletes; BridgeBase
        // overrides to redirect into BridgeArena. The virtual call here
        // costs no more than the virtual-dtor call already paid by
        // `delete this`, so retain()/release() arithmetic stays non-virtual.
        virtual void destroy() noexcept { delete this; }

    private:
        uint32_t refcount_{0};
    };

    template <typename T>
    class IntrusivePtr
    {
        static_assert(std::is_base_of_v<RefCounted, T>,
                      "IntrusivePtr<T> requires T to inherit from value::RefCounted");

    public:
        IntrusivePtr() noexcept = default;

        explicit IntrusivePtr(T* raw) noexcept : ptr_(raw)
        {
            if (ptr_) ptr_->retain();
        }

        IntrusivePtr(const IntrusivePtr& other) noexcept : ptr_(other.ptr_)
        {
            if (ptr_) ptr_->retain();
        }

        IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_)
        {
            other.ptr_ = nullptr;
        }

        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        IntrusivePtr(const IntrusivePtr<U>& other) noexcept : ptr_(other.get())
        {
            if (ptr_) ptr_->retain();
        }

        ~IntrusivePtr() noexcept
        {
            if (ptr_) ptr_->release();
        }

        IntrusivePtr& operator=(const IntrusivePtr& other) noexcept
        {
            if (this != &other)
            {
                if (other.ptr_) other.ptr_->retain();
                if (ptr_) ptr_->release();
                ptr_ = other.ptr_;
            }
            return *this;
        }

        IntrusivePtr& operator=(IntrusivePtr&& other) noexcept
        {
            if (this != &other)
            {
                if (ptr_) ptr_->release();
                ptr_ = other.ptr_;
                other.ptr_ = nullptr;
            }
            return *this;
        }

        void reset(T* raw = nullptr) noexcept
        {
            if (ptr_ == raw) return;
            if (raw) raw->retain();
            if (ptr_) ptr_->release();
            ptr_ = raw;
        }

        T* release() noexcept
        {
            T* tmp = ptr_;
            ptr_ = nullptr;
            return tmp;
        }

        T* get() const noexcept { return ptr_; }
        T& operator*() const noexcept { return *ptr_; }
        T* operator->() const noexcept { return ptr_; }
        explicit operator bool() const noexcept { return ptr_ != nullptr; }

        friend bool operator==(const IntrusivePtr& a, const IntrusivePtr& b) noexcept
        {
            return a.ptr_ == b.ptr_;
        }
        friend bool operator!=(const IntrusivePtr& a, const IntrusivePtr& b) noexcept
        {
            return a.ptr_ != b.ptr_;
        }

    private:
        T* ptr_{nullptr};
    };

    template <typename T, typename... Args>
    IntrusivePtr<T> makeIntrusive(Args&&... args)
    {
        return IntrusivePtr<T>(new T(std::forward<Args>(args)...));
    }
}
