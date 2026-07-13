#include "engine/onnx_model.hpp"

#include <array>

namespace snapback {

OnnxModel& OnnxModel::instance() {
    static OnnxModel model;
    return model;
}

std::optional<std::filesystem::path> OnnxModel::resolve_model_path(
    const std::filesystem::path& app_data_dir) {
    // Rust checks the app data dir, then the training-export dir. Match both, in order.
    for (const std::filesystem::path candidate :
         {app_data_dir / "model.onnx",
          app_data_dir / "exports" / "training" / "model.onnx"}) {
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return std::nullopt;
}

#if defined(SNAPBACK_ONNX)

bool OnnxModel::init(const std::filesystem::path& model_path) {
    try {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "snapback");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        // The wide-string overload is the portable way to pass a path on Windows.
        session_ = std::make_unique<Ort::Session>(*env_, model_path.wstring().c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        input_name_ = session_->GetInputNameAllocated(0, allocator).get();
        output_names_.clear();
        const std::size_t outputs = session_->GetOutputCount();
        for (std::size_t i = 0; i < outputs; ++i) {
            output_names_.push_back(session_->GetOutputNameAllocated(i, allocator).get());
        }
        model_path_ = model_path.string();
        loaded_ = true;
    } catch (const std::exception&) {
        // A bad/missing/incompatible model must not crash startup; fall back to heuristic.
        session_.reset();
        env_.reset();
        model_path_.reset();
        loaded_ = false;
    }
    return loaded_;
}

PredictionScores OnnxModel::run(const FeatureVector& features) {
    if (!loaded_ || !session_) return {};
    try {
        std::array<float, kFeatureCount> input{};
        for (std::size_t i = 0; i < kFeatureCount; ++i) {
            input[i] = static_cast<float>(features.values[i]);
        }
        std::array<int64_t, 2> shape{1, static_cast<int64_t>(kFeatureCount)};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            mem, input.data(), input.size(), shape.data(), shape.size());

        const char* input_names[] = {input_name_.c_str()};
        std::vector<const char*> output_names;
        output_names.reserve(output_names_.size());
        for (const auto& name : output_names_) output_names.push_back(name.c_str());

        auto outputs = session_->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1,
                                     output_names.data(), output_names.size());

        // Find the 4-class probability tensor among the outputs (mirrors Rust's
        // extract_probas: some exporters also emit a label tensor we skip).
        for (auto& out : outputs) {
            if (!out.IsTensor()) continue;
            const auto info = out.GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) continue;
            if (info.GetElementCount() != 4) continue;
            const float* data = out.GetTensorData<float>();
            std::array<double, 4> probas{data[0], data[1], data[2], data[3]};
            // Neutral thrash/drift/goal: the classifier applies its guardrails afterward.
            return scores_from_probabilities(probas, 0.0, 0.0, 0.5);
        }
    } catch (const std::exception&) {
        // Inference failure -> empty scores; the classifier treats this as no signal.
    }
    return {};
}

void OnnxModel::reset_for_tests() {
    loaded_ = false;
    model_path_.reset();
    session_.reset();
    env_.reset();
    input_name_.clear();
    output_names_.clear();
}

#else  // stub build (SNAPBACK_ONNX off)

bool OnnxModel::init(const std::filesystem::path&) {
    loaded_ = false;
    return false;
}

PredictionScores OnnxModel::run(const FeatureVector&) { return {}; }

void OnnxModel::reset_for_tests() {
    loaded_ = false;
    model_path_.reset();
}

#endif

}  // namespace snapback
