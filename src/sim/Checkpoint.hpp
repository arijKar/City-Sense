#pragma once
#include <string>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <cstdint>
#include <utility>  

namespace sim {

/*
simple serialisable simulator state
- now: current simulated time
- batches_processed: how many batches we already sent through the pipeline
*/
struct CheckpointState {
    std::chrono::system_clock::time_point now{};
    std::uint64_t batches_processed{0};
};

/*
small helper to persist simulator state into a binary file
- saves and loads CheckpointState from disk
- lets the simulator resume from last run instead of restarting from zero
- binary format: [int64 nanoseconds since epoch][uint64 batches_processed]
field: path filesystem path of the checkpoint file (default "checkpoint.bin")
*/
struct Checkpoint {
    std::string path{"checkpoint.bin"};

    explicit Checkpoint(std::string p = "checkpoint.bin")
      : path(std::move(p)) {}

/*
save current simulator state to disk
- opens file in binary + trunc mode
- converts time_point to nanoseconds since epoch (int64)
- writes nanoseconds and batches_processed in a fixed order
- throws if file cannot be opened or write fails
param: state simulator state to persist
throws: std::runtime_error if file open or write fails
*/
    void save(const CheckpointState& state) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Checkpoint: cannot open file for writing: " + path);
        }

        // serialize time_point as a count of nanoseconds since epoch + batch count
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

/*
load simulator state from disk
- opens file in binary mode
- reads nanoseconds and batch counter in same order as save()
- converts nanoseconds back to system_clock::time_point
- throws if file cannot be opened or read fails
return: CheckpointState restored from the file
throws: std::runtime_error if file open or read fails
*/
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

        // convert stored nanoseconds to the clock's native duration type
        sys_duration d = std::chrono::duration_cast<sys_duration>(
            std::chrono::nanoseconds{ns64}
        );
        state.now = sys_clock::time_point{d};
        state.batches_processed = batches;
        return state;
    }
};

} // namespace sim
