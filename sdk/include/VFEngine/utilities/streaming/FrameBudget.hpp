#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace streaming
{
    // Per-frame work allowance shared by the streaming systems: a count budget
    // and an optional byte budget, consumed together. Call reset() once per frame.
    class FrameBudget
    {
    public:
        FrameBudget() = default;

        explicit FrameBudget(int maxCount,
                             size_t maxBytes = std::numeric_limits<size_t>::max())
        {
            configure(maxCount, maxBytes);
        }

        void configure(int maxCount,
                       size_t maxBytes = std::numeric_limits<size_t>::max())
        {
            this->maxCount = maxCount;
            this->maxBytes = maxBytes;
            reset();
        }

        void reset()
        {
            countLeft = maxCount;
            bytesLeft = maxBytes;
        }

        // Consume one unit of work (optionally with a byte cost). Returns false —
        // and consumes nothing — when either allowance would be exceeded.
        bool tryConsume(int count = 1, size_t bytes = 0)
        {
            if (count > countLeft || bytes > bytesLeft)
                return false;
            countLeft -= count;
            bytesLeft -= bytes;
            return true;
        }

        [[nodiscard]] bool exhausted() const { return countLeft <= 0 || bytesLeft == 0; }
        [[nodiscard]] int countRemaining() const { return countLeft; }
        [[nodiscard]] size_t bytesRemaining() const { return bytesLeft; }
        [[nodiscard]] int countLimit() const { return maxCount; }

    private:
        int maxCount = 0;
        size_t maxBytes = std::numeric_limits<size_t>::max();
        int countLeft = 0;
        size_t bytesLeft = std::numeric_limits<size_t>::max();
    };

} // namespace streaming
