// ONNX Runtime inference. Rust: engine/onnx_model.rs (behind `--features onnx`).
//
// This is a case where C++ is *easier* than Rust: ONNX Runtime ships a first-class C++
// API, so there's no `ort` wrapper crate to fight. Gated by the SNAPBACK_ONNX CMake
// option (a PUBLIC define on snapback_core), mirroring the Rust cargo feature. When the
// option is off, this is a stub whose loaded() is always false and the classifier falls
// back to the heuristic.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "engine/classifier.hpp"
#include "engine/features.hpp"

#if defined(SNAPBACK_ONNX)
#include <memory>
#include <vector>

#include <onnxruntime_cxx_api.h>
#endif

namespace snapback {

class OnnxModel {
public:
    static OnnxModel& instance();

    // Rust: onnx_model::init(model_path). Loads once at startup if the file exists.
    bool init(const std::filesystem::path& model_path);
    bool loaded() const { return loaded_; }
    // Path of the loaded model (for HealthStatus), or nullopt when none is loaded.
    const std::optional<std::string>& model_path() const { return model_path_; }

    // Rust: onnx_model::resolve_model_path(app_data_dir).
    static std::optional<std::filesystem::path> resolve_model_path(
        const std::filesystem::path& app_data_dir);

    // Runs the 31-feature vector through the graph -> scores. Returns default-constructed
    // scores if no model is loaded (the classifier only calls this when loaded()).
    // Non-const because Ort::Session::Run mutates session state.
    PredictionScores run(const FeatureVector& features);

    // Test seam (Rust: reset_model_for_tests) — the singleton persists across tests, so a
    // test that loads a model must reset afterward or it leaks the "onnx" backend.
    void reset_for_tests();

private:
    bool loaded_ = false;
    std::optional<std::string> model_path_;  // set on successful load
#if defined(SNAPBACK_ONNX)
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::vector<std::string> output_names_;
#endif
};

}  // namespace snapback
