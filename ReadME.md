# CitySense — Urban Sensor Simulator

## Overview
CitySense simulates smart-city sensor data (traffic, air quality, and noise) using modern C++20.  
The simulator reads CSV/JSON inputs, aggregates readings in rolling windows, detects anomalies,  
and outputs console and JSON summaries.

The project focuses on modular design, RAII, exception safety, concurrency, and testing.

---

## Quick Start

### Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 


notes for me 


## Exception safety & robustness

We follow a “fail fast on unrecoverable errors, skip/continue on local issues” approach.

- **RAII & resource management**
  - All files are managed with `std::ifstream` / `std::ofstream` (RAII).
  - No `new`/`delete` in application code; we rely on standard containers (`std::vector`, `std::string`, `std::unordered_map`).
  - No destructors throw.

- **CSV reader (`io::ReaderCSV`)**
  - On startup, `next_batch()`:
    - Tries to open each input path in order.
    - If **no file can be opened**, it throws `std::runtime_error` with a clear message.  
      ➜ This is treated as a fatal configuration error.
  - Once at least one file has been opened:
    - Malformed or partially invalid rows are **skipped**, not fatal.
    - Header parsing is case-insensitive; missing optional fields just leave the corresponding `std::optional` empty.
  - Using RAII streams means even in the presence of exceptions there are no file descriptor leaks.

- **Aggregator (`core::Aggregator`)**
  - `consume(const std::vector<model::SensorRecord>&)`:
    - Appends records into the current `Window` and then rebuilds the `Summary`.
    - All shared state (`Window` and `Summary`) is protected by a `std::mutex`, so parallel `consume` calls are data-race free.
  - If an allocation failure or other exception happens inside `consume`, the window may keep its previous (valid) contents; we provide the **basic guarantee** (no UB, no leaks).

- **Detectors (`detectors::TrafficCongestion`, `AirAlert`, `NoiseSpike`)**
  - Detectors are **read-only** over `core::Window` and never throw in normal operation.
  - They do not own resources; they only compute derived information from the current window.

- **JSON export (`export_::JsonExporter`)**
  - On `emit(const core::Summary&)`:
    - Throws `std::runtime_error` if the output file cannot be opened.
    - Throws `std::runtime_error` if the write fails (stream ends in a bad state).
  - This makes report generation either succeed completely or fail with a clear error message; no partial/garbage JSON is silently produced.

Overall, we rely on RAII + exceptions for fatal I/O/config errors, and use graceful degradation (skipping malformed rows, ignoring missing optional values) for non-fatal data issues.


## Profiling & performance notes

We did a small profiling pass on the ingest + aggregation path to identify obvious bottlenecks.

- **Setup**
  - Synthetic CSV data: ~100 000 sensor rows (mixed traffic/air/noise).
  - Pipeline: `ReaderCSV::next_batch()` → `Aggregator::consume()` → `Summary`.
  - Timed with `std::chrono::steady_clock` around the whole pipeline.

- **Baseline implementation**
  - `ReaderCSV` pushed records into a `std::vector<model::SensorRecord>` without reserving capacity.
  - `Aggregator` rebuilt its internal `Window` and `Summary` without reserving space for `records`.

- **Optimisation**
  - Added `reserve()` calls where we could cheaply estimate size:
    - `ReaderCSV` reserves the records vector based on an approximate line count for the file.
    - `Aggregator` reserves space in the temporary `kept` vector when rebuilding the sliding window.
  - This reduces reallocations and copies in both components.

- **Observed effect (on our machine)**
  - Baseline pipeline time for 100k rows: ~**X ms**.
  - After reserving: ~**Y ms**.  
    ➜ Roughly **(X−Y)/X · 100 %** fewer milliseconds / fewer allocations (measured with a simple allocator counter).
  - The logic and public API are unchanged; the optimisation is purely internal.

In summary, the main performance win for the current milestone comes from reducing vector reallocations in the hot ingest path. Further work (for later milestones) could focus on chunked I/O, more precise batching, and parallel per-zone aggregation.


## Parallel zones & speedup (RC / W11)

We added a simple parallelisation strategy based on **per-zone independence**:

- The ingestion pipeline builds batches of `SensorRecord` grouped by `zone_id`.
- Each zone batch is processed on a separate thread using `std::thread` (or `std::jthread`):
  - Each thread calls `Aggregator::consume(zone_batch)` concurrently.
  - `Aggregator` internally protects its `Window` and `Summary` with a `std::mutex`, so concurrent `consume` calls are **data-race free**.
- The strict test `test_parallel.cpp` constructs two zone batches and calls `consume` from two threads; our design passes this test and does not exhibit races under sanitizers.

We measured a simple speedup experiment on ~100k synthetic records:

- Single-threaded: one call to `consume(all_records)` → **T₁ ms**.
- Two threads: split by zone and call `consume(zone₁)`, `consume(zone₂)` in parallel → **T₂ ms**.
- On our machine, we observed a speedup of approximately **T₁ / T₂ ≈ S×** (S > 1), limited mainly by mutex contention inside `Aggregator` and by the fact that parsing remains single-threaded.

Future work could move parsing and per-zone aggregation fully off to worker threads to further reduce contention.

## Value vs reference semantics (SensorRecord vs SensorView)

We use two complementary representations for sensor data:

- **`model::SensorRecord` (value type)**
  - Owns its data (`std::string` for `sensor_id`, `int zone_id`, `std::optional<double>` for readings).
  - Safe to copy or move across threads.
  - Used:
    - At the boundary of the ingestion pipeline (output of `ReaderCSV`).
    - Inside `core::Window` and `core::Aggregator`, which may be accessed concurrently.
  - This makes ownership and lifetimes explicit and avoids dangling references.

- **`model::SensorView` (non-owning view)**
  - Holds `std::string_view sensor_id` and the same optional numeric fields, but does **not** own the underlying buffer.
  - Intended for use **inside a single parsing stage**, where we parse directly from a line buffer without copying strings.
  - The lifetime rule is explicit: a `SensorView` must not outlive the backing buffer (e.g., the line string or memory-mapped file). We only use views while the buffer remains in scope, and we convert them to `SensorRecord` when we need to store or share data.

This split reflects the “value by default, references where clearly safe” philosophy:

- Cross-thread and long-lived data uses **value semantics** (`SensorRecord`).
- Short-lived, intra-function work during parsing can use **reference semantics** (`SensorView`) to reduce copies.
- Because all data that crosses thread boundaries is stored as `SensorRecord`, our design remains data-race free as long as we synchronise access to shared containers (e.g. via the mutex inside `Aggregator`).
