#pragma once

#include "FrameAllocator.hpp"
#include <cstring>
#include <new>
#include <initializer_list>
#include <algorithm>
#include <type_traits>

namespace memory {

	template<typename T>
	class FrameVector {
	public:
		explicit FrameVector(FrameAllocator& allocator)
			: allocator(allocator) {}

		FrameVector(FrameAllocator& allocator, uint32_t reserveCount)
			: allocator(allocator) {
			reserve(reserveCount);
		}

		// No destructor needed - FrameAllocator owns the memory and resets per frame

		void reserve(uint32_t newCapacity) {
			if (newCapacity <= cap) return;

			auto handle = allocator.allocate(
				static_cast<uint64_t>(newCapacity) * sizeof(T), alignof(T));
			if (!handle.isValid()) return;

			T* newData = static_cast<T*>(allocator.getPointer(handle));

			// Move existing elements
			if (data && count > 0) {
				if constexpr (std::is_trivially_copyable_v<T>) {
					std::memcpy(newData, data, count * sizeof(T));
				} else {
					for (uint32_t i = 0; i < count; ++i) {
						new (&newData[i]) T(std::move(data[i]));
						data[i].~T();
					}
				}
			}

			data = newData;
			cap = newCapacity;
		}

		void push_back(const T& value) {
			if (count >= cap) {
				reserve(cap == 0 ? 16 : cap * 2);
			}
			if constexpr (std::is_trivially_copyable_v<T>) {
				data[count] = value;
			} else {
				new (&data[count]) T(value);
			}
			count++;
		}

		void push_back(T&& value) {
			if (count >= cap) {
				reserve(cap == 0 ? 16 : cap * 2);
			}
			new (&data[count]) T(std::move(value));
			count++;
		}

		template<typename... Args>
		T& emplace_back(Args&&... args) {
			if (count >= cap) {
				reserve(cap == 0 ? 16 : cap * 2);
			}
			new (&data[count]) T(std::forward<Args>(args)...);
			return data[count++];
		}

		void clear() {
			if constexpr (!std::is_trivially_destructible_v<T>) {
				for (uint32_t i = 0; i < count; ++i) {
					data[i].~T();
				}
			}
			count = 0;
		}

		void sort() {
			std::sort(data, data + count);
		}

		template<typename Compare>
		void sort(Compare comp) {
			std::sort(data, data + count, comp);
		}

		T& operator[](uint32_t index) { return data[index]; }
		const T& operator[](uint32_t index) const { return data[index]; }

		T* begin() { return data; }
		T* end() { return data + count; }
		const T* begin() const { return data; }
		const T* end() const { return data + count; }

		uint32_t size() const { return count; }
		uint32_t capacity() const { return cap; }
		bool empty() const { return count == 0; }
		T* getData() { return data; }
		const T* getData() const { return data; }

		T& front() { return data[0]; }
		const T& front() const { return data[0]; }
		T& back() { return data[count - 1]; }
		const T& back() const { return data[count - 1]; }

	private:
		FrameAllocator& allocator;
		T* data = nullptr;
		uint32_t count = 0;
		uint32_t cap = 0;
	};

}
