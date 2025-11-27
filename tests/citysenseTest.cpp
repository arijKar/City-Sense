#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "io/ReaderCSV.hpp"
#include "io/ReaderJSON.hpp"
#include "core/Aggregator.hpp"
#include "core/Window.hpp"
#include "detectors/TrafficCongestion.hpp"
#include "detectors/AirAlert.hpp"
#include "detectors/NoiseSpike.hpp"
#include "model/SensorRecord.hpp"
#include "sim/Clock.hpp"
#include "sim/Replayer.hpp"
#include "sim/Checkpoint.hpp"

// -----------------
// Tiny test helpers
// -----------------

int g_failures = 0;

#define EXPECT_TRUE(cond)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__              \
                      << " EXPECT_TRUE(" #cond ")\n";                           \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define EXPECT_EQ(a, b)                                                         \
    do {                                                                        \
        auto _va = (a);                                                         \
        auto _vb = (b);                                                         \
        if (!(_va == _vb)) {                                                    \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__              \
                      << " EXPECT_EQ(" #a ", " #b ") got [" << _va << "] vs ["  \
                      << _vb << "]\n";                                          \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

// For doubles where we expect rounding differences
#define EXPECT_NEAR(a, b, eps)                                                  \
    do {                                                                        \
        double _va = static_cast<double>(a);                                    \
        double _vb = static_cast<double>(b);                                    \
        if (std::abs(_va - _vb) > (eps)) {                                      \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__              \
                      << " EXPECT_NEAR(" #a ", " #b ", " #eps                   \
                      << ") got [" << _va << "] vs [" << _vb << "]\n";          \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

// --------------------
// 1. Parsing / Reader
// --------------------

// helper to write a small file for tests
static std::string write_temp_file(const std::string& name,
                                   const std::string& contents) {
    std::string path = "tmp_" + name;
    std::ofstream out(path);
    out << contents;
    return path;
}

// Good parsing tests:
// • Test broken CSV/JSON lines.
// • Ensure no crashes.
// • Ensure records are safe.

void test_csv_parsing_broken_lines() {
    // 1) Create a small CSV with one broken line.
    const std::string csv =
        "timestamp,sensor_id,zone_id,speed,flow,pm25,pm10,db\n"
        "2025-10-13T08:00:00Z,A1,1,50,100,22,30,70\n"
        "BROKEN_LINE_WITH_TOO_FEW_FIELDS\n"
        "2025-10-13T08:01:00Z,A1,1,40,90,25,32,75\n";

    std::string csv_path = write_temp_file("broken.csv", csv);

    // 2) Config that points to that CSV (same structure as main).
    const std::string cfg_json =
        "{\n"
        "  \"window_minutes\": 10,\n"
        "  \"inputs\": [\n"
        "    { \"path\": \"" + csv_path + "\", \"type\": \"csv\" }\n"
        "  ]\n"
        "}\n";

    std::string cfg_path = write_temp_file("broken_config.json", cfg_json);

    io::ReaderJSON cfg_reader(cfg_path);
    io::Config cfg = cfg_reader.read();

    io::ReaderCSV reader(cfg.inputs);

    auto batch = reader.next_batch(10);

    // We expect the two good lines to be parsed.
    EXPECT_EQ(batch.size(), std::size_t{2});
    EXPECT_EQ(batch[0].zone_id, 1);
    EXPECT_EQ(batch[1].zone_id, 1);
}

// JSON parsing: just check we can handle good + weird JSON without crashing.
// We don't force ReaderJSON to throw, because your implementation may just
// treat bad fields as defaults.

void test_json_parsing_good_and_bad() {
    // good config
    const std::string good_cfg =
        "{\n"
        "  \"window_minutes\": 10,\n"
        "  \"inputs\": [\n"
        "    { \"path\": \"data/air.csv\", \"type\": \"csv\" }\n"
        "  ]\n"
        "}\n";

    std::string good_path = write_temp_file("config_good.json", good_cfg);
    {
        io::ReaderJSON cfg_reader(good_path);
        io::Config cfg = cfg_reader.read();
        EXPECT_EQ(cfg.window_minutes, 10);
        EXPECT_TRUE(!cfg.inputs.empty());  // at least one input
    }

    // "Bad" config: wrong field names / types.
    // Good test: calling read() should not crash.
    const std::string bad_cfg =
        "{\n"
        "  \"windowMinutes\": \"oops string\",\n"
        "  \"inputs\": {}\n"
        "}\n";

    std::string bad_path = write_temp_file("config_bad.json", bad_cfg);
    {
        bool threw = false;
        try {
            io::ReaderJSON bad_reader(bad_path);
            (void)bad_reader.read();
        } catch (const std::exception&) {
            threw = true;
        }
        // Either is fine: throwing or using defaults.
        // We don't assert on 'threw' here; we only care we didn't crash.
        (void)threw;
    }
}

// ---------------------
// 2. Aggregation / stats
// ---------------------

void test_aggregator_basic_summary() {
    core::Aggregator agg(/*window_minutes=*/5);

    std::vector<model::SensorRecord> v;

    auto mk_ts = [](int minute) {
        using namespace std::chrono;
        return std::chrono::system_clock::time_point{} +
               std::chrono::minutes{minute};
    };

    // 3 records, 2 zones
    model::SensorRecord r1;
    r1.ts = mk_ts(0);
    r1.zone_id = 1;
    v.push_back(r1);

    model::SensorRecord r2;
    r2.ts = mk_ts(1);
    r2.zone_id = 1;
    v.push_back(r2);

    model::SensorRecord r3;
    r3.ts = mk_ts(10); // far in the future -> others will be evicted
    r3.zone_id = 2;
    v.push_back(r3);

    agg.consume(v);
    core::Summary s = agg.summary();

    // only r3 should remain in the 5-minute sliding window
    EXPECT_EQ(s.total_count, 1);
    EXPECT_EQ(s.by_zone.size(), std::size_t{1});
    EXPECT_EQ(s.by_zone.at(2), 1);
}

#include "detectors/AirAlert.hpp"

void test_basic_stats_mean_median_p90_p99() {
    using detectors::BasicStats;
    using detectors::compute_basic_stats;

    std::vector<double> vals{10, 20, 30, 40, 50};

    BasicStats s = compute_basic_stats(vals);

    EXPECT_NEAR(s.mean,   30.0, 1e-6);
    EXPECT_NEAR(s.median, 30.0, 1e-6);
    EXPECT_NEAR(s.p90,    46.0, 1e-6);   // 90% position
    EXPECT_NEAR(s.p99,    49.6, 1e-6);   // close to max
}

// -----------------
// 3. Detector tests
// -----------------

void test_airalert_trigger_and_non_trigger() {
    core::Window w;
    std::vector<std::string> alerts;

    detectors::AirAlert det(/*pm25_thr=*/35.0, &alerts);

    auto mk_ts = [](int minute) {
        using namespace std::chrono;
        return std::chrono::system_clock::time_point{} +
               std::chrono::minutes{minute};
    };

    // Case 1: all values below threshold -> no alert
    {
        w.records.clear();
        alerts.clear();

        model::SensorRecord r1;
        r1.ts = mk_ts(0);
        r1.pm25 = 20.0;
        w.records.push_back(r1);

        model::SensorRecord r2;
        r2.ts = mk_ts(1);
        r2.pm25 = 25.0;
        w.records.push_back(r2);

        det.detect(w);
        EXPECT_TRUE(alerts.empty());
    }

    // Case 2: high values -> alert
    {
        w.records.clear();
        alerts.clear();

        model::SensorRecord r1;
        r1.ts = mk_ts(0);
        r1.pm25 = 40.0;
        w.records.push_back(r1);

        model::SensorRecord r2;
        r2.ts = mk_ts(1);
        r2.pm25 = 50.0;
        w.records.push_back(r2);

        det.detect(w);
        EXPECT_EQ(alerts.size(), std::size_t{1});
        EXPECT_TRUE(alerts[0].find("High PM2.5") != std::string::npos);
    }
}

#include "detectors/NoiseSpike.hpp"
#include "detectors/TrafficCongestion.hpp"

void test_noise_and_traffic_detectors() {
    auto mk_ts = [](int minute) {
        using namespace std::chrono;
        return std::chrono::system_clock::time_point{} +
               std::chrono::minutes{minute};
    };

    core::Window w;
    std::vector<std::string> alerts;

    // Noise: require at least 2 loud events in a 10-minute window
    detectors::NoiseSpike noise_det(/*db_thr=*/80.0,
                                    /*count_thr=*/2,
                                    /*window_min=*/10,
                                    &alerts);

    // Traffic: 2 consecutive minutes below 40 km/h
    detectors::TrafficCongestion traffic_det(/*speed_thr_kmh=*/40.0,
                                             /*consec_min=*/2,
                                             &alerts);

    // Non-trigger case
    {
        w.records.clear();
        alerts.clear();

        model::SensorRecord r1;
        r1.ts = mk_ts(0);
        r1.db = 60.0;       // not loud
        r1.speed = 50.0;    // fine
        w.records.push_back(r1);

        noise_det.detect(w);
        traffic_det.detect(w);
        EXPECT_TRUE(alerts.empty());
    }

    // Trigger both: loud noise + low speeds for 2 minutes
    {
        w.records.clear();
        alerts.clear();

        model::SensorRecord r1;
        r1.ts = mk_ts(0);
        r1.db = 85.0;
        r1.speed = 30.0;
        w.records.push_back(r1);

        model::SensorRecord r2;
        r2.ts = mk_ts(5);
        r2.db = 90.0;
        r2.speed = 35.0;
        w.records.push_back(r2);

        noise_det.detect(w);
        traffic_det.detect(w);

        EXPECT_TRUE(alerts.size() >= 2);
        bool has_noise = false;
        bool has_traffic = false;
        for (const auto& msg : alerts) {
            if (msg.find("NoiseSpike") != std::string::npos) has_noise = true;
            if (msg.find("TrafficCongestion") != std::string::npos) has_traffic = true;
        }
        EXPECT_TRUE(has_noise);
        EXPECT_TRUE(has_traffic);
    }
}

// -------------------
// 4. Concurrency test
// -------------------

void test_aggregator_concurrency_single_vs_multi() {
    core::Aggregator agg_single(/*window_minutes=*/60);
    core::Aggregator agg_multi (/*window_minutes=*/60);

    auto mk_ts = [](int minute) {
        using namespace std::chrono;
        return std::chrono::system_clock::time_point{} +
               std::chrono::minutes{minute};
    };

    // Build a big batch of records for zones 1..4
    std::vector<model::SensorRecord> all;
    for (int zone = 1; zone <= 4; ++zone) {
        for (int i = 0; i < 50; ++i) {
            model::SensorRecord r;
            r.ts = mk_ts(i);
            r.zone_id = zone;
            all.push_back(r);
        }
    }

    // single-thread: one big consume
    agg_single.consume(all);

    // multi-thread: group by zone and feed with threads
    std::unordered_map<int, std::vector<model::SensorRecord>> per_zone;
    for (auto& rec : all) {
        per_zone[rec.zone_id].push_back(rec);
    }

    std::vector<std::thread> threads;
    for (auto& [zone, records] : per_zone) {
        (void)zone;
        threads.emplace_back(
            [&agg_multi, local = std::move(records)]() mutable {
                agg_multi.consume(local);
            });
    }
    for (auto& t : threads) {
        t.join();
    }

    core::Summary s1 = agg_single.summary();
    core::Summary s2 = agg_multi.summary();

    EXPECT_EQ(s1.total_count, s2.total_count);
    EXPECT_EQ(s1.by_zone.size(), s2.by_zone.size());
    for (const auto& [zone, count] : s1.by_zone) {
        EXPECT_EQ(count, s2.by_zone.at(zone));
    }
}

// -----------------------
// 5. Record vs View tests
// -----------------------

void test_record_copy_move_safety() {
    model::SensorRecord r1;
    r1.zone_id = 42;

    // copy
    model::SensorRecord r2 = r1;
    EXPECT_EQ(r2.zone_id, 42);

    // move
    model::SensorRecord r3 = std::move(r1);
    EXPECT_EQ(r3.zone_id, 42);
}

// ---------------
// 6. Simulator tests
// ---------------

void test_checkpoint_save_load_roundtrip() {
    const std::string path = "test_checkpoint.bin";
    sim::Checkpoint cp{path};

    sim::CheckpointState state;
    state.now = std::chrono::system_clock::time_point{
        std::chrono::seconds{12345}
    };
    state.batches_processed = 7;

    cp.save(state);
    sim::CheckpointState loaded = cp.load();

    EXPECT_EQ(loaded.batches_processed, state.batches_processed);

    auto d1 = state.now.time_since_epoch();
    auto d2 = loaded.now.time_since_epoch();
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(d1).count(),
              std::chrono::duration_cast<std::chrono::seconds>(d2).count());
}

void test_replayer_advances_clock_and_calls_processor() {
    using namespace std::chrono;

    // Fake reader that returns 3 batches and then empty
    struct FakeReader {
        int calls = 0;
        std::vector<int> next_batch(std::size_t batch_size) {
            (void)batch_size;
            if (calls >= 3) return {};
            ++calls;
            return std::vector<int>{calls}; // batch with 1 element
        }
    };

    sim::Replayer replayer;
    sim::Clock clock(system_clock::time_point{}, /*step_seconds=*/60);
    FakeReader reader;

    int processed_count = 0;
    system_clock::time_point start = clock.now();

    replayer.run(
        clock,
        reader,
        [&](sim::Clock& clk, std::vector<int>& batch) {
            (void)clk;
            processed_count += static_cast<int>(batch.size());
        },
        /*batch_size=*/10,
        /*max_ticks=*/-1
    );

    EXPECT_EQ(processed_count, 3); // three batches
    auto dt = duration_cast<minutes>(clock.now() - start);
    EXPECT_EQ(dt.count(), 3);      // 3 ticks of 60 seconds
}

// -------------------
// Pretty runner
// -------------------

static void run_test(const char* name, void (*fn)()) {
    std::cout << "[TEST] " << name << "...\n";
    int before = g_failures;
    fn();
    if (g_failures == before) {
        std::cout << "  -> OK\n\n";
    } else {
        std::cout << "  -> FAILED\n\n";
    }
}

// -------------------
// main: run all tests
// -------------------

int main() {
    std::cout << "========== CitySense custom test suite ==========\n\n";

    run_test("CSV parsing with broken lines",        test_csv_parsing_broken_lines);
    run_test("JSON parsing (good + bad)",            test_json_parsing_good_and_bad);
    run_test("Aggregator basic summary",             test_aggregator_basic_summary);
    run_test("Stats: mean / median / p90 / p99",     test_basic_stats_mean_median_p90_p99);
    run_test("AirAlert trigger / non-trigger",       test_airalert_trigger_and_non_trigger);
    run_test("NoiseSpike / TrafficCongestion",       test_noise_and_traffic_detectors);
    run_test("Aggregator concurrency single/multi",  test_aggregator_concurrency_single_vs_multi);
    run_test("SensorRecord copy / move",             test_record_copy_move_safety);
    run_test("Checkpoint save/load roundtrip",       test_checkpoint_save_load_roundtrip);
    run_test("Replayer & Clock basic",               test_replayer_advances_clock_and_calls_processor);

    if (g_failures == 0) {
        std::cout << "✅ All tests passed.\n";
        return 0;
    } else {
        std::cout << "❌ Tests failed: " << g_failures << "\n";
        return 1;
    }
}
