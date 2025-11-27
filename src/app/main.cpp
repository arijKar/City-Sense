#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <exception>
#include <iomanip>
#include <ctime>
#include <chrono>

#include "io/ReaderCSV.hpp"
#include "io/ReaderJSON.hpp"
#include "core/Aggregator.hpp"
#include "core/Window.hpp"
#include "detectors/TrafficCongestion.hpp"
#include "detectors/AirAlert.hpp"
#include "detectors/NoiseSpike.hpp"
#include "export/ConsolePrinter.hpp"
#include "export/JsonExporter.hpp"
#include "model/SensorRecord.hpp"
#include "sim/Clock.hpp"
#include "sim/Replayer.hpp"
#include "sim/Checkpoint.hpp"

int main(int argc, char** argv) {
    try {
        // --- 1. Load config (JSON) ---
        std::string config_path = (argc > 1) ? argv[1] : "config.json";
        io::ReaderJSON cfg_reader(config_path);
        io::Config cfg = cfg_reader.read();

        if (cfg.inputs.empty()) {
            std::cerr << "Config has no inputs; nothing to do.\n";
            return 1;
        }

        // --- 2. Set up pipeline components ---
        io::ReaderCSV csv_reader(cfg.inputs);
        core::Aggregator aggregator(cfg.window_minutes);
        std::vector<std::string> alerts;

        detectors::TrafficCongestion traffic_det{
            /*speed_thr=*/40.0,
            /*consec_min=*/3,
            &alerts
        };
        detectors::AirAlert air_det{
            /*pm25_thr=*/35.0,
            &alerts
        };
        detectors::NoiseSpike noise_det{
            /*db_thr=*/80.0,
            /*count_thr=*/5,
            /*window_min=*/10,
            &alerts
        };

        std::vector<core::Detector*> detectors;
        detectors.push_back(&traffic_det);
        detectors.push_back(&air_det);
        detectors.push_back(&noise_det);

        export_::ConsolePrinter console_printer;
        export_::JsonExporter   json_exporter("report.json");

        // --- 3. Simulator: clock + checkpoint + replayer ---

        constexpr std::size_t BATCH_SIZE   = 1000;
        constexpr int         STEP_SECONDS = 60; 

        sim::Checkpoint      checkpoint{"checkpoint.bin"};
        sim::CheckpointState cp_state{};

        bool resume = false;
        try {
            cp_state = checkpoint.load();
            resume = true;
        } catch (...) {
            cp_state.now = std::chrono::system_clock::now();
            cp_state.batches_processed = 0;
        }

        if (resume) {
            std::cout << "[sim] Resuming simulation time from checkpoint. "
                      << "batches_processed=" << cp_state.batches_processed << "\n";
        } else {
            std::cout << "[sim] Starting fresh simulation.\n";
        }

        sim::Clock    clock(cp_state.now, STEP_SECONDS);
        sim::Replayer replayer;

        replayer.run(
            clock,
            csv_reader,
            [&](sim::Clock& clk, std::vector<model::SensorRecord>& batch) {
                // --- simple log for each simulation tick ---
                auto tp = clk.now();
                std::time_t time_value =
                    std::chrono::system_clock::to_time_t(tp);

                // portable conversion to UTC tm
                std::tm utc_tm{};
            #ifdef _MSC_VER
                // MSVC "safe" version
                gmtime_s(&utc_tm, &time_value);
            #else
                if (auto* p = std::gmtime(&time_value)) {
                    utc_tm = *p;
                }
            #endif

                std::cout << "[sim] tick " << cp_state.batches_processed
                          << " | time=" << std::put_time(&utc_tm, "%Y-%m-%d %H:%M:%S UTC")
                          << " | batch_size=" << batch.size() << "\n";

                // 1) group current batch by zone_id
                std::unordered_map<int, std::vector<model::SensorRecord>> per_zone;
                per_zone.reserve(batch.size());
                for (auto& rec : batch) {
                    per_zone[rec.zone_id].push_back(rec);
                }

                // 2) one thread per zone, each calls aggregator.consume(...)
                std::vector<std::thread> threads;
                threads.reserve(per_zone.size());
                for (auto& [zone, records] : per_zone) {
                    (void)zone;
                    threads.emplace_back(
                        [&aggregator, local = std::move(records)]() mutable {
                            aggregator.consume(local);
                        }
                    );
                }
                for (auto& th : threads) {
                    th.join();
                }

                // 3) run detectors on current window (read-only)
                const core::Window& window = aggregator.current_window_view();
                for (auto* det : detectors) {
                    det->detect(window);
                }

                // 4) update and save simulator state (for pause/resume *time info*)
                cp_state.now = clk.now();
                ++cp_state.batches_processed;
                checkpoint.save(cp_state);
            },
            BATCH_SIZE
            // max_ticks = default (-1) -> run until reader is empty
        );

        std::cout << "[sim] Finished replay. Total batches processed = "
                  << cp_state.batches_processed << "\n\n";

        // --- 4. Final summary & reports ---
        core::Summary summary = aggregator.summary();

        // Console report: summary + same alerts as JSON
        console_printer.emit(summary, alerts);

        // JSON report: consistent with console
        json_exporter.emit(summary, alerts);

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown error\n";
        return 1;
    }
}
