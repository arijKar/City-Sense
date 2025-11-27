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
param: clock, reader, process_batch, batch_size, max_ticks      
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
            
            if (max_ticks >= 0 && tick >= max_ticks) {
                break;
            }

            auto batch = reader.next_batch(batch_size);
            if (batch.empty()) {
                break; 
            }

            process_batch(clock, batch);

            clock.advance();
            ++tick;
        }
    }
};

} // namespace sim
