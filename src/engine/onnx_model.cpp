#include "engine/onnx_model.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace snapback {

OnnxModel& OnnxModel::instance() {
    static OnnxModel model;
    return model;
}

std::optional<std::filesystem::path> OnnxModel::resolve_model_path(
    const std::filesystem::path& app_data_dir) {
    // Check the app data dir, then the training-export dir, in that order.
    for (const std::filesystem::path candidate :
         {app_data_dir / "model.onnx",
          app_data_dir / "exports" / "training" / "model.onnx"}) {
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return std::nullopt;
}

std::optional<std::string> OnnxModel::model_id_for_path(
    const std::filesystem::path& model_path) {
    if (!std::filesystem::is_regular_file(model_path)) return std::nullopt;

    std::ifstream input(model_path, std::ios::binary);
    if (!input) return std::nullopt;

    // FNV-1a is deliberately implemented here instead of adding a crypto dependency for a
    // local provenance identifier. The feature-contract prefix makes the identity useful even
    // when a future model happens to share the same file hash.
    std::uint64_t hash = 14695981039346656037ull;
    char byte = 0;
    while (input.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    if (!input.eof()) return std::nullopt;

    std::ostringstream id;
    id << "onnx:" << kFeatureContractId << ":" << std::hex << std::setfill('0')
       << std::setw(16) << hash;
    return id.str();
}

void OnnxModel::unload() {
    loaded_ = false;
    model_path_.reset();
    model_id_.reset();
#if defined(SNAPBACK_ONNX)
    session_.reset();
    env_.reset();
    input_name_.clear();
    output_names_.clear();
#endif
}

#if defined(SNAPBACK_ONNX)

bool OnnxModel::init(const std::filesystem::path& model_path) {
    try {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "snapback");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
#if defined(_WIN32)
        // ONNX Runtime exposes the wide-string path overload on Windows.
        const auto native_model_path = model_path.wstring();
#else
        // POSIX builds expose the UTF-8 path overload instead; wstring() does not compile
        // against Ort::Session there.
        const auto native_model_path = model_path.string();
#endif
        session_ = std::make_unique<Ort::Session>(*env_, native_model_path.c_str(), options);

        const auto identity = model_id_for_path(model_path);
        if (!identity) throw std::runtime_error("could not read model identity");

        Ort::AllocatorWithDefaultOptions allocator;
        input_name_ = session_->GetInputNameAllocated(0, allocator).get();
        output_names_.clear();
        const std::size_t outputs = session_->GetOutputCount();
        for (std::size_t i = 0; i < outputs; ++i) {
            output_names_.push_back(session_->GetOutputNameAllocated(i, allocator).get());
        }
        model_path_ = model_path.string();
        model_id_ = identity;
        loaded_ = true;
    } catch (const std::exception&) {
        // A bad/missing/incompatible model must not crash startup; fall back to heuristic.
        session_.reset();
        env_.reset();
        model_path_.reset();
        model_id_.reset();
        loaded_ = false;
    }
    return loaded_;
}

std::optional<std::array<double, 4>> OnnxModel::infer_probabilities(const FeatureVector& features) {
    if (!loaded_ || !session_) return std::nullopt;
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

        // Find the 4-class probability tensor among the outputs. Some exporters also
        // emit a label tensor we skip.
        for (auto& out : outputs) {
            if (!out.IsTensor()) continue;
            const auto info = out.GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) continue;
            if (info.GetElementCount() != 4) continue;
            const float* data = out.GetTensorData<float>();
            // Raw probabilities only. The classifier combines them with the user's context
            // signals; doing it here is what dropped Block rules and goal alignment.
            return std::array<double, 4>{data[0], data[1], data[2], data[3]};
        }
    } catch (const std::exception&) {
        // Fall through to nullopt: the classifier retries on the heuristic path.
    }
    // Also reached when no output was a 4-element float tensor — a model whose outputs we
    // can't interpret is a failure, not a neutral prediction.
    return std::nullopt;
}

void OnnxModel::reset_for_tests() {
    unload();
}

#else  // stub build (SNAPBACK_ONNX off)

bool OnnxModel::init(const std::filesystem::path&) {
    loaded_ = false;
    return false;
}

std::optional<std::array<double, 4>> OnnxModel::infer_probabilities(const FeatureVector&) {
    return std::nullopt;
}

void OnnxModel::reset_for_tests() {
    unload();
}

#endif

}  // namespace snapback
