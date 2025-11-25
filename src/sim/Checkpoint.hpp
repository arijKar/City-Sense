#pragma once
#include <string>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <cstdint>
#include <utility>  

namespace sim {

// Simple serialisable simulator state.
struct CheckpointState {
    std::chrono::system_clock::time_point now{};
    std::uint64_t batches_processed{0};
};

// Minimal helper for persisting simulator state to a binary file.
struct Checkpoint {
    std::string path{"checkpoint.bin"};

    explicit Checkpoint(std::string p = "checkpoint.bin")
      : path(std::move(p)) {}

    void save(const CheckpointState& state) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Checkpoint: cannot open file for writing: " + path);
        }

        // Serialize time_point as a count of nanoseconds since epoch + batch count.
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            state.now.time_since_epoch())
                            .count();
        std::int64_t ns64 = static_cast<std::int64_t>(ns);
        out.write(reinterpret_cast<const char*>(&ns64), sizeof(ns64));
        out.write(reinterpret_cast<const char*>(&state.batches_processed),
                  sizeof(state.batches_processed));

        if (!out) {
            throw std::runtime_error("Checkpoint: write failed for " + path);
        }
    }

    CheckpointState load() const {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Checkpoint: cannot open file for reading: " + path);
        }

        std::int64_t ns64 = 0;
        std::uint64_t batches = 0;
        in.read(reinterpret_cast<char*>(&ns64), sizeof(ns64));
        in.read(reinterpret_cast<char*>(&batches), sizeof(batches));

        if (!in) {
            throw std::runtime_error("Checkpoint: read failed for " + path);
        }

        CheckpointState state;

        using sys_clock = std::chrono::system_clock;
        using sys_duration = sys_clock::duration;

        // Convert stored nanoseconds to the clock's native duration type
        sys_duration d = std::chrono::duration_cast<sys_duration>(
            std::chrono::nanoseconds{ns64}
        );
        state.now = sys_clock::time_point{d};
        state.batches_processed = batches;
        return state;
    }
};

} // namespace sim
