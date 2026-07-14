#include <exception>
#include <filesystem>
#include <iostream>
#include <vector>

#include "engine/feature_parity.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: snapback_feature_parity_export <scenarios.json>\n";
        return 2;
    }

    try {
        const auto scenarios = std::filesystem::path(argv[1]);
        const std::vector<snapback::AppRuleRecord> rules;
        const auto results = snapback::replay_feature_parity_file(scenarios, rules);
        std::cout << snapback::feature_parity_results_to_json(results).dump(2) << '\n';
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "feature parity export failed: " << err.what() << '\n';
        return 1;
    }
}
