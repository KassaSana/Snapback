// Isolated Storage::open cost, not full GUI startup-to-ready. Build the
// snapback_startup_benchmarks target. Argument: days (default 30), 1000 aged
// prediction rows per day; 21 freshly copied fixtures, warm OS cache.
#include "storage/storage.hpp"
#include "bench_util.hpp"
#include <filesystem>
#include <iostream>
using namespace snapback;
using namespace snapback::bench;
int main(int argc, char** argv) {
    const int days = argc > 1 ? std::stoi(argv[1]) : 30;
    if (days < 1 || days > 365) return 1;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("snapback_startup_measure_" + std::to_string(days) + "_" + std::to_string(unique));
    std::filesystem::create_directories(root / "seed");
    {
        auto store = Storage::open(root / "seed");
        if (!store) return 2;
        const auto session = store->create_session("startup fixture", FocusMode::Normal);
        Storage::Transaction tx(*store);
        for (int i = 0; i < days * 1000; ++i) {
            PredictionRecord p;
            p.session_id = session.session_id;
            p.focus_score = 75;
            p.distraction_risk = .1;
            p.focus_state = "PRODUCTIVE";
            p.timestamp_ms = 1577836800000LL + static_cast<std::int64_t>(i) * 1000;
            store->insert_prediction(p);
        }
        tx.commit();
    }
    std::vector<double> samples;
    const Timer wall;
    for (int i = 0; i < 21; ++i) {
        const auto trial = root / ("trial" + std::to_string(i));
        std::filesystem::create_directories(trial);
        std::filesystem::copy_file(root / "seed" / "focoflow.db", trial / "focoflow.db", std::filesystem::copy_options::overwrite_existing);
        Timer timer;
        auto store = Storage::open(trial);
        if (!store) return 3;
        samples.push_back(timer.elapsed_us());
    }
    print_stats("Storage open aged fixture", samples.size(), summarize(samples, wall.elapsed_ms()));
    std::filesystem::remove_all(root);
}
