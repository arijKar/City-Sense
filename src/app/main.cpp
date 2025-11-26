#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
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
        // Load config file
        std::string config_path = (argc > 1) ? argv[1] : "config.json";

        io::ReaderJSON cfg_reader(config_path);
        io::Config cfg = cfg_reader.read();

        if (cfg.inputs.empty()) {
            std::cerr << "Config has no inputs; nothing to do.\n";
            return 1;
        }

        // readers and core pipeline pieces
        io::ReaderCSV csv_reader(cfg.inputs);
        core::Aggregator aggregator(cfg.window_minutes);

        // detectors
        detectors::TrafficCongestion traffic_det{/*speed_thr=*/40.0,
                                                 /*consec_min=*/3};
        detectors::AirAlert           air_det{/*pm25_thr=*/35.0};
        detectors::NoiseSpike         noise_det{/*db_thr=*/80.0,
                                                /*count_thr=*/5,
                                                /*window_min=*/10};

        std::vector<core::Detector*> detectors;
        detectors.push_back(&traffic_det);
        detectors.push_back(&air_det);
        detectors.push_back(&noise_det);

        // exporters for the final summary
        export_::ConsolePrinter console_printer;
        export_::JsonExporter   json_exporter("report.json");

        // Main ingestion loop 
        constexpr std::size_t BATCH_SIZE = 1000;

        for (;;) {
            auto batch = csv_reader.next_batch(BATCH_SIZE);
            if (batch.empty()) {
                break; 
            }

            
            aggregator.consume(batch);

            
            const core::Window& window = aggregator.current_window_view();
            for (auto* det : detectors) {
                det->detect(window);
            }
        }

        // summary at the end of the run
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
