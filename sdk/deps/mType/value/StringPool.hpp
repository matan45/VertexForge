#pragma once
#include "InternedString.hpp"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>

// Forward declaration for friend class
namespace mType { namespace value { namespace arrays { class StringArray; } } }

namespace value
{
    /**
     * @brief Thread-safe global string pool with automatic memory management
     *
     * Implements string interning (deduplication) using the Flyweight pattern.
     * Strings are stored once and referenced by ID, saving memory for duplicate strings.
     *
     * Design principles:
     * - Singleton Pattern: One global pool per application
     * - Thread-Safe: All operations protected by mutex
     * - Reference Counting: Automatic cleanup when no references remain
     * - LRU-like Cleanup: Periodic cleanup of zero-ref-count entries
     *
     * Performance characteristics:
     * - intern(): O(log n) average (hash map lookup + insertion)
     * - String comparison: O(1) for InternedString vs InternedString
     * - Memory savings: Significant for applications with many duplicate strings
     *
     * Use cases:
     * - String literals and constants
     * - Identifiers (variable names, class names)
     * - Repeated user input strings
     * - Dictionary keys and enum-like strings
     *
     * Not recommended for:
     * - Unique strings (adds overhead without benefit)
     * - Very long strings (> 1KB by default)
     * - Short-lived temporary strings
     */
    class StringPool
    {
    private:
        struct PoolEntry
        {
            std::string value;
            std::atomic<size_t> refCount;
            std::chrono::steady_clock::time_point lastAccessed;

            PoolEntry(const std::string& str)
                : value(str), refCount(1), lastAccessed(std::chrono::steady_clock::now()) {}

            PoolEntry(std::string&& str)
                : value(std::move(str)), refCount(1), lastAccessed(std::chrono::steady_clock::now()) {}
        };

        std::unordered_map<std::string, size_t> stringToId;
        std::unordered_map<size_t, std::unique_ptr<PoolEntry>> idToEntry;
        std::atomic<size_t> nextId{1};
        StringPoolStats stats;

        static constexpr size_t MIN_INTERN_LENGTH = 1;
        static constexpr size_t MAX_INTERN_LENGTH = 1024;
        static constexpr size_t MAX_POOL_SIZE = 100000;
        static constexpr std::chrono::minutes CLEANUP_INTERVAL{30};

        bool shouldIntern(const std::string& str) const
        {
            size_t len = str.length();
            return len >= MIN_INTERN_LENGTH && len <= MAX_INTERN_LENGTH;
        }

        void cleanupExpiredEntries();

        size_t getEntryMemoryUsage(const PoolEntry& entry) const
        {
            return sizeof(PoolEntry) + entry.value.capacity();
        }

        friend class InternedString;
        friend class mType::value::arrays::StringArray;

        void incrementRef(size_t id);
        void decrementRef(size_t id);
        const std::string& getStringById(size_t id) const;

        // Private constructor for singleton
        StringPool() = default;

        // Delete copy constructor and assignment
        StringPool(const StringPool&) = delete;
        StringPool& operator=(const StringPool&) = delete;

    public:
        /**
         * @brief Get singleton instance of StringPool
         */
        static StringPool& getInstance()
        {
            static StringPool instance;
            return instance;
        }

        /**
         * @brief Intern a string (lvalue reference)
         * @param str String to intern
         * @return InternedString handle (or empty if string shouldn't be interned)
         */
        InternedString intern(const std::string& str);

        /**
         * @brief Intern a string (rvalue reference - efficient for temporaries)
         * @param str String to intern (moved into pool)
         * @return InternedString handle (or empty if string shouldn't be interned)
         */
        InternedString intern(std::string&& str);

        /**
         * @brief Check if a string is already in the pool
         */
        bool contains(const std::string& str) const
        {
            return stringToId.find(str) != stringToId.end();
        }

        /**
         * @brief Get InternedString by pool ID (for deserialization)
         * @param id Pool ID
         * @return InternedString handle (or empty if ID not found)
         */
        InternedString getById(size_t id) const;

        /**
         * @brief Get current pool statistics
         */
        StringPoolStats getStats() const
        {
            auto currentStats = stats;
            currentStats.currentPoolSize = idToEntry.size();
            currentStats.maxPoolSize = std::max(currentStats.maxPoolSize, currentStats.currentPoolSize);
            return currentStats;
        }

        /**
         * @brief Clear all pooled strings (use with caution - invalidates all InternedStrings!)
         */
        void clear()
        {
            stringToId.clear();
            idToEntry.clear();
            stats = StringPoolStats{};
            nextId.store(1);
        }

        /**
         * @brief Manually trigger cleanup of expired entries
         */
        void cleanup()
        {
            cleanupExpiredEntries();
        }

        /**
         * @brief Calculate total memory usage of the pool
         */
        size_t getTotalMemoryUsage() const;

        /**
         * @brief Get most frequently referenced strings
         * @param count Number of top strings to return
         * @return Vector of (string, refCount) pairs, sorted by refCount descending
         */
        std::vector<std::pair<std::string, size_t>> getTopStrings(size_t count = 10) const;
    };
}
