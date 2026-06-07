#pragma once
#include <string>
#include <cstddef>

// Forward declarations
namespace value
{
    class StringPool;

    /**
     * @brief Statistics for string pool performance tracking
     */
    struct StringPoolStats
    {
        size_t totalRequests = 0;
        size_t poolHits = 0;
        size_t poolMisses = 0;
        size_t currentPoolSize = 0;
        size_t totalMemorySaved = 0;
        size_t maxPoolSize = 0;

        double getHitRate() const {
            return totalRequests > 0 ?
                   static_cast<double>(poolHits) / totalRequests : 0.0;
        }

        double getMemoryEfficiency() const {
            return currentPoolSize > 0 ?
                   static_cast<double>(totalMemorySaved) / currentPoolSize : 0.0;
        }
    };

    /**
     * @brief Lightweight reference to a pooled string
     *
     * Represents a reference-counted string stored in the global StringPool.
     * Provides transparent string access while saving memory through deduplication.
     *
     * Memory model:
     * - Empty string: ID = 0 (special case, no pool reference)
     * - Pooled string: ID > 0 (reference to StringPool entry)
     * - Reference counting: Automatic increment/decrement on copy/destroy
     *
     * Performance characteristics:
     * - Size: 16 bytes (vs ~32 bytes for std::string)
     * - Copy: O(1) with atomic ref count increment
     * - Comparison: O(1) for InternedString, O(n) vs std::string
     * - Access: O(1) through pool lookup
     */
    class InternedString
    {
    private:
        size_t poolId;
        StringPool* pool;

        friend class StringPool;

        // Private constructor for StringPool to create instances
        InternedString(size_t id, StringPool* p);

    public:
        /**
         * @brief Default constructor creates empty string (ID = 0)
         */
        InternedString();

        /**
         * @brief Copy constructor with reference counting
         */
        InternedString(const InternedString& other);

        /**
         * @brief Move constructor (efficient, no ref count change)
         */
        InternedString(InternedString&& other) noexcept;

        /**
         * @brief Copy assignment with reference counting
         */
        InternedString& operator=(const InternedString& other);

        /**
         * @brief Move assignment (efficient, no ref count change)
         */
        InternedString& operator=(InternedString&& other) noexcept;

        /**
         * @brief Destructor decrements reference count
         */
        ~InternedString();

        /**
         * @brief Get the actual string value from pool
         * @return Reference to pooled string (or empty string if ID = 0)
         */
        const std::string& getString() const;

        /**
         * @brief Implicit conversion to std::string reference
         */
        operator const std::string&() const { return getString(); }

        // Comparison operators
        bool operator==(const InternedString& other) const {
            return poolId == other.poolId && pool == other.pool;
        }

        bool operator!=(const InternedString& other) const {
            return !(*this == other);
        }

        bool operator==(const std::string& str) const {
            return getString() == str;
        }

        bool operator!=(const std::string& str) const {
            return getString() != str;
        }

        // String concatenation operators
        std::string operator+(const std::string& str) const {
            return getString() + str;
        }

        std::string operator+(const InternedString& other) const {
            return getString() + other.getString();
        }

        /**
         * @brief Hash value for use in hash containers
         * Uses pool ID for O(1) hashing
         */
        size_t hash() const noexcept { return poolId; }

        // String-like interface
        bool empty() const { return poolId == 0; }
        size_t size() const { return getString().size(); }
        size_t length() const { return size(); }

        /**
         * @brief Get internal pool ID (for debugging/diagnostics)
         */
        size_t getPoolId() const { return poolId; }
    };
}

// Global operators for string concatenation
namespace value
{
    inline std::string operator+(const std::string& lhs, const InternedString& rhs)
    {
        return lhs + rhs.getString();
    }
}

// Standard library hash specialization
namespace std
{
    template<>
    struct hash<value::InternedString>
    {
        size_t operator()(const value::InternedString& is) const noexcept
        {
            return is.hash();
        }
    };
}
