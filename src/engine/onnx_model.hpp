// ONNX Runtime inference, behind the SNAPBACK_ONNX build option. When off, loaded() is always
// false and the classifier uses the heuristic.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
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

    // Loads once at startup if the file exists.
    bool init(const std::filesystem::path& model_path);
    void unload();
    bool loaded() const { return loaded_; }
    // Path of the loaded model (for HealthStatus), or nullopt when none is loaded.
    const std::optional<std::string>& model_path() const { return model_path_; }
    // Stable identity for the loaded model: feature contract plus a content hash.
    const std::optional<std::string>& model_id() const { return model_id_; }

    // Computes the identity without loading ONNX Runtime. Used by deployment/storage tests
    // and by init() after the runtime has accepted the model.
    static std::optional<std::string> model_id_for_path(
        const std::filesystem::path& model_path);

    // Resolves only the quality-gated deployed model from the app data directory.
    // Training-export candidates are never runtime-loadable.
    static std::optional<std::filesystem::path> resolve_model_path(
        const std::filesystem::path& app_data_dir);

    // Raw class probabilities [DISTRACTED, PSEUDO_PRODUCTIVE, PRODUCTIVE, DEEP_FOCUS] for the
    // 31-feature vector. nullopt means inference failed and the caller falls back to the
    // heuristic. Probabilities, not scores: scoring needs the user's context, which this layer
    // does not know. Non-const because Ort::Session::Run mutates state.
    std::optional<std::array<double, 4>> infer_probabilities(const FeatureVector& features);

    // Usable class weights: all finite and non-negative, at least one positive. Not required to
    // sum to 1 (the classifier normalises), so an unnormalised linear head is valid.
    static bool valid_class_probabilities(const std::array<double, 4>& probas);

    // Inference health, separate from load health: a loaded model can still fail every Run().
    // Both reset on load/unload.
    bool last_inference_failed() const {
        return last_inference_failed_.load(std::memory_order_relaxed);
    }
    std::uint64_t inference_failures() const {
        return inference_failures_.load(std::memory_order_relaxed);
    }

    // Test seam — the singleton persists across tests, so a
    // test that loads a model must reset afterward or it leaks the "onnx" backend.
    void reset_for_tests();

    // Applies the validator and the failure bookkeeping to one raw model output. Split from
    // infer_probabilities so the stub build (no runtime) can test the contract with
    // hand-built rows; infer_probabilities is Run() plus this.
    std::optional<std::array<double, 4>> accept_output(
        std::optional<std::array<double, 4>> raw);

private:
    bool loaded_ = false;
    // Atomics rather than mutex-guarded: written on the engine thread, read by the health
    // snapshot, and a slightly stale counter is harmless while a lock here is not free.
    std::atomic<bool> last_inference_failed_{false};
    std::atomic<std::uint64_t> inference_failures_{0};
    std::optional<std::string> model_path_;  // set on successful load
    std::optional<std::string> model_id_;    // set on successful load
#if defined(SNAPBACK_ONNX)
    // The Run() call and output-tensor search, minus validation. Throws are caught inside
    // and reported as nullopt.
    std::optional<std::array<double, 4>> run_session(const FeatureVector& features);

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::vector<std::string> output_names_;
#endif
};

}  // namespace snapback
