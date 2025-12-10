#pragma once
#include <cstdint>
#include <random>
#include <functional>

namespace uuid {

	class UUID {
	public:
		// Default constructor generates a new random UUID
		UUID() : value(generateRandom()) {}

		// Construct from existing value
		explicit UUID(uint64_t id) : value(id) {}

		// Invalid/null UUID
		static UUID invalid() { return UUID(0); }

		// Check if valid (non-zero)
		bool isValid() const { return value != 0; }

		// Get the raw value
		uint64_t getValue() const { return value; }

		// Comparison operators
		bool operator==(const UUID& other) const { return value == other.value; }
		bool operator!=(const UUID& other) const { return value != other.value; }
		bool operator<(const UUID& other) const { return value < other.value; }

		// For use in hash maps
		struct Hash {
			size_t operator()(const UUID& uuid) const {
				return std::hash<uint64_t>{}(uuid.value);
			}
		};

	private:
		uint64_t value;

		static uint64_t generateRandom() {
			static std::random_device rd;
			static std::mt19937_64 generator(rd());
			static std::uniform_int_distribution<uint64_t> distribution(1, UINT64_MAX);
			return distribution(generator);
		}
	};

}
