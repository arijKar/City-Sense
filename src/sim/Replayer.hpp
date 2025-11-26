#pragma once

#include <cstddef>
#include <utility>
#include <functional>
#include "Clock.hpp"

namespace sim {

/*
time-accelerated replay helper
- drives a Clock forward in discrete "ticks"
- repeatedly pulls batches from a Reader (must have next_batch(size_t))
- hands each batch to a user-supplied processing function
- lets us simulate real-time ingestion using test data or recorded data
*/
struct Replayer {
/*
main replay loop
- keeps calling reader.next_batch(batch_size)
- stops when:
  - max_ticks is reached (if max_ticks >= 0), or
  - reader returns an empty batch (end of input)
- for each non-empty batch:
  - calls process_batch(clock, batch) so the caller can push data into the pipeline
  - advances the clock by one tick using clock.advance()
- tick count is mainly for tests / demos so we can limit how long we run
param: clock          simulated clock that we advance every iteration
param: reader         object with next_batch(batch_size) method
param: process_batch  callable with signature like (Clock&, BatchType&)
param: batch_size     number of elements to request from reader per tick
param: max_ticks      max number of iterations (default -1 = no limit)
*/
    template <typename Reader, typename Processor>
    void run(Clock& clock,
             Reader& reader,
             Processor&& process_batch,
             std::size_t batch_size,
             int max_ticks = -1) const
    {
        int tick = 0;

        for (;;) {
            // optional limit on ticks (for tests / demo)
            if (max_ticks >= 0 && tick >= max_ticks) {
                break;
            }

            auto batch = reader.next_batch(batch_size);
            if (batch.empty()) {
                break; // end of input
            }

            // let the caller feed this batch into the pipeline
            process_batch(clock, batch);

            // advance simulated time by one step
            clock.advance();
            ++tick;
        }
    }
};

} // namespace sim
