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

        // vector that will collect all alert lines from detectors
        std::vector<std::string> alerts;

        // Detectors share the same alert sink
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

        // --- 3. Ingestion loop (batch by batch) ---
        constexpr std::size_t BATCH_SIZE = 1000;

        for (;;) {
            auto batch = csv_reader.next_batch(BATCH_SIZE);
            if (batch.empty()) {
                break; // no more data
            }

            // group current batch by zone_id
            std::unordered_map<int, std::vector<model::SensorRecord>> per_zone;
            per_zone.reserve(batch.size());
            for (auto& rec : batch) {
                per_zone[rec.zone_id].push_back(rec);
            }

            // one thread per zone, each thread calls aggregator.consume(...)
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
            for (auto& t : threads) {
                t.join();
            }

            // Run detectors on current window (read-only view)
            const core::Window& window = aggregator.current_window_view();
            for (auto* det : detectors) {
                det->detect(window);
            }
        }

        // --- 4. Final summary & reports ---
        core::Summary summary = aggregator.summary();

        // Console report: summary + all detected events
        console_printer.emit(summary, alerts);

        // JSON report: same summary + same alerts (consistent with console)
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
