#pragma once

#include <cstddef>
#include <utility>
#include <functional>
#include "Clock.hpp"

namespace sim {

// Time-accelerated replay helper.
//
// It drives a Clock forward and repeatedly pulls batches from a Reader,
// handing them to a user-supplied processing function.
struct Replayer {
    template <typename Reader, typename Processor>
    void run(Clock& clock,
             Reader& reader,
             Processor&& process_batch,
             std::size_t batch_size,
             int max_ticks = -1) const
    {
        int tick = 0;

        for (;;) {
            // Optional limit on ticks (for tests / demo).
            if (max_ticks >= 0 && tick >= max_ticks) {
                break;
            }

            auto batch = reader.next_batch(batch_size);
            if (batch.empty()) {
                break; // end of input
            }

            // Let the caller feed this batch into the pipeline.
            process_batch(clock, batch);

            // Advance simulated time by one step.
            clock.advance();
            ++tick;
        }
    }
};

} // namespace sim
