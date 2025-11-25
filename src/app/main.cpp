#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <exception>

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

        // Detectors – you can tweak thresholds later if needed.
        detectors::TrafficCongestion traffic_det{/*speed_thr=*/40.0, /*consec_min=*/3};
        detectors::AirAlert air_det{/*pm25_thr=*/35.0};
        detectors::NoiseSpike noise_det{/*db_thr=*/80.0,
                                        /*count_thr=*/5,
                                        /*window_min=*/10};

        std::vector<core::Detector*> detectors;
        detectors.push_back(&traffic_det);
        detectors.push_back(&air_det);
        detectors.push_back(&noise_det);

        export_::ConsolePrinter console_printer;
        export_::JsonExporter json_exporter("report.json");

        // --- 3. Ingestion loop with per-zone parallelism ---
        constexpr std::size_t BATCH_SIZE = 1000;

        for (;;) {
            auto batch = csv_reader.next_batch(BATCH_SIZE);
            if (batch.empty()) {
                break; // all inputs exhausted
            }

            // Partition batch by zone_id
            std::unordered_map<int, std::vector<model::SensorRecord>> per_zone;
            per_zone.reserve(batch.size());
            for (auto& rec : batch) {
                per_zone[rec.zone_id].push_back(rec);
            }

            // Parallel ingest per zone
            std::vector<std::thread> threads;
            threads.reserve(per_zone.size());
            for (auto& [zone, records] : per_zone) {
                (void)zone; // zone is only used as key; not needed inside lambda
                threads.emplace_back([&aggregator, local = std::move(records)]() mutable {
                    // Each thread consumes its own local vector
                    aggregator.consume(local);
                });
            }
            for (auto& t : threads) {
                t.join();
            }

            // Run detectors on current window view (read-only)
            const core::Window& window = aggregator.current_window_view();
            for (auto* det : detectors) {
                det->detect(window);
            }
        }

        // --- 4. Final summary & reports ---
        core::Summary summary = aggregator.summary();
        console_printer.emit(summary);
        json_exporter.emit(summary);

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown error\n";
        return 1;
    }
}
