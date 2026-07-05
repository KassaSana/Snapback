#[cfg(feature = "onnx")]
use parking_lot::Mutex;
#[cfg(feature = "onnx")]
use std::path::{Path, PathBuf};
#[cfg(feature = "onnx")]
use std::sync::OnceLock;

#[cfg(feature = "onnx")]
use ort::session::Session;
#[cfg(feature = "onnx")]
use ort::value::Tensor;

#[cfg(feature = "onnx")]
use crate::engine::classifier::build_prediction_scores;
#[cfg(feature = "onnx")]
use crate::engine::features::FeatureVector;

#[cfg(feature = "onnx")]
static MODEL: OnceLock<Mutex<Option<Session>>> = OnceLock::new();

#[cfg(feature = "onnx")]
fn model_slot() -> &'static Mutex<Option<Session>> {
    MODEL.get_or_init(|| Mutex::new(None))
}

#[cfg(feature = "onnx")]
pub fn resolve_model_path(app_data_dir: &Path) -> Option<PathBuf> {
    [
        app_data_dir.join("model.onnx"),
        app_data_dir.join("exports").join("training").join("model.onnx"),
    ]
    .into_iter()
    .find(|path| path.is_file())
}

#[cfg(feature = "onnx")]
pub fn init(path: &Path) -> Result<(), String> {
    let bytes = std::fs::read(path).map_err(|err| format!("failed to read {}: {err}", path.display()))?;
    let session = Session::builder()
        .map_err(|err| err.to_string())?
        .commit_from_memory(&bytes)
        .map_err(|err| format!("failed to load {}: {err}", path.display()))?;

    *model_slot().lock() = Some(session);
    Ok(())
}

#[cfg(feature = "onnx")]
pub fn is_loaded() -> bool {
    model_slot().lock().is_some()
}

#[cfg(all(test, feature = "onnx"))]
pub(crate) fn reset_model_for_tests() {
    *model_slot().lock() = None;
}

#[cfg(feature = "onnx")]
fn extract_probas(outputs: &ort::session::SessionOutputs) -> Option<[f64; 4]> {
    for value in outputs.values() {
        let Ok((shape, data)) = value.try_extract_tensor::<f32>() else {
            continue;
        };

        let flat: Vec<f32> = data.iter().copied().collect();
        if flat.len() == 4 {
            return Some([
                flat[0] as f64,
                flat[1] as f64,
                flat[2] as f64,
                flat[3] as f64,
            ]);
        }

        if shape.len() == 2 && shape[1] == 4 && !flat.is_empty() {
            return Some([
                flat[0] as f64,
                flat[1] as f64,
                flat[2] as f64,
                flat[3] as f64,
            ]);
        }
    }
    None
}

#[cfg(feature = "onnx")]
pub fn predict(
    features: &FeatureVector,
    thrash: f64,
    drift: f64,
    goal_alignment: f64,
) -> Option<crate::engine::classifier::PredictionScores> {
    let mut guard = model_slot().lock();
    let session = guard.as_mut()?;

    let input_values = features.training_input();
    let input = Tensor::from_array(([1usize, input_values.len()], input_values.to_vec()))
        .ok()?;
    let outputs = session.run(ort::inputs![input]).ok()?;
    let probas = extract_probas(&outputs)?;

    Some(build_prediction_scores(probas, thrash, drift, goal_alignment))
}

#[cfg(not(feature = "onnx"))]
pub fn resolve_model_path(_app_data_dir: &std::path::Path) -> Option<std::path::PathBuf> {
    None
}

#[cfg(not(feature = "onnx"))]
pub fn is_loaded() -> bool {
    false
}

#[cfg(not(feature = "onnx"))]
pub fn init(_path: &std::path::Path) -> Result<(), String> {
    Err("ONNX support not enabled; rebuild with --features onnx".to_string())
}

#[cfg(not(feature = "onnx"))]
use crate::engine::features::FeatureVector;

#[cfg(not(feature = "onnx"))]
pub fn predict(
    _features: &FeatureVector,
    _thrash: f64,
    _drift: f64,
    _goal_alignment: f64,
) -> Option<crate::engine::classifier::PredictionScores> {
    None
}

#[cfg(all(test, feature = "onnx"))]
mod tests {
    use std::path::Path;

    use super::*;

    #[test]
    fn predict_without_loaded_model_returns_none() {
        reset_model_for_tests();
        let features = FeatureVector::empty(0.0);
        assert!(predict(&features, 0.1, 0.2, 0.5).is_none());
    }

    #[test]
    fn fixture_model_predict_returns_productive_scores() {
        reset_model_for_tests();
        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../fixtures/model.onnx");
        assert!(
            path.is_file(),
            "missing {}; run: python tools/generate_onnx_fixture.py",
            path.display()
        );

        init(&path).expect("load fixtures/model.onnx");
        assert!(is_loaded());

        let mut features = FeatureVector::empty(1.0);
        features.keystroke_count = 10;
        let scores = predict(&features, 0.1, 0.2, 0.5).expect("onnx predict");
        assert_eq!(scores.focus_state, "PRODUCTIVE");
        assert!(scores.focus_score > 0.0);
        assert!(scores.focus_score <= 100.0);
    }
}

#[cfg(test)]
mod training_input_tests {
    use crate::engine::features::FeatureVector;

    #[test]
    fn training_input_has_31_values_in_training_column_order() {
        let features = FeatureVector {
            keystroke_count: 8,
            keystroke_rate: 2.5,
            is_ide: true,
            focus_momentum: 0.75,
            ..FeatureVector::empty(1_700_000_000.0)
        };
        let input = features.training_input();
        assert_eq!(input.len(), 31);
        assert_eq!(input[4], 8.0);
        assert!((input[5] - 2.5).abs() < f32::EPSILON);
        assert_eq!(input[25], 1.0);
        assert!((input[29] - 0.75).abs() < f32::EPSILON);
    }
}
