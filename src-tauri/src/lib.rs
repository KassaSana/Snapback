mod bench;
mod capture;
mod commands;
mod engine;
mod events;
mod label_shortcuts;
mod smoke;
mod snapback;
mod state;
mod storage;
mod training_deploy;
mod tray;
mod types;

use tauri::Manager;

use state::AppState;
use storage::Storage;

static PROCESS_START: std::sync::OnceLock<std::time::Instant> = std::sync::OnceLock::new();

pub fn run_from_cli(args: Vec<String>) -> i32 {
    let _ = PROCESS_START.set(std::time::Instant::now());

    if args.iter().any(|a| a == "--benchmark") {
        let bench_args = bench::parse_bench_args(&args);
        return bench::run_benchmark(bench_args);
    }

    if let Some(idx) = args.iter().position(|a| a == "--feature-parity") {
        let path = args
            .get(idx + 1)
            .map(std::path::PathBuf::from)
            .unwrap_or_else(|| std::path::PathBuf::from("fixtures/feature_parity/scenarios.json"));
        return crate::engine::parity::run_feature_parity(&path);
    }

    if args.iter().any(|a| a == "--classifier-eval") {
        return crate::engine::classifier_eval::run_classifier_eval_cli(&args);
    }

    if args.iter().any(|a| a == "--smoke") {
        return crate::smoke::run_smoke_cli(&args);
    }

    if let Some(idx) = args
        .iter()
        .position(|a| a == "--export-feature-parity-json")
    {
        let path = args
            .get(idx + 1)
            .map(std::path::PathBuf::from)
            .unwrap_or_else(|| std::path::PathBuf::from("fixtures/feature_parity/scenarios.json"));
        let rules = Vec::new();
        return match crate::engine::parity::export_feature_parity_json(&path, &rules) {
            Ok(json) => {
                print!("{json}");
                0
            }
            Err(err) => {
                eprintln!("{err}");
                1
            }
        };
    }

    run();
    0
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::init();

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
            if let Some(t0) = PROCESS_START.get() {
                log::info!("startup_ms_to_setup={}", t0.elapsed().as_millis());
            }

            let app_data_dir = app.path().app_data_dir()?;
            #[cfg(feature = "onnx")]
            if let Some(model_path) = engine::onnx_model::resolve_model_path(&app_data_dir) {
                match engine::onnx_model::init(&model_path) {
                    Ok(()) => log::info!("loaded ONNX model from {}", model_path.display()),
                    Err(err) => log::warn!("ONNX model load failed: {err}"),
                }
            }
            let storage = Storage::open(app_data_dir)?;
            let app_state = AppState::new(storage);
            app.manage(app_state);

            let handle = app.handle().clone();
            if let Some(state) = handle.try_state::<AppState>() {
                let _ = state.start_engine(handle.clone());
            }

            tray::setup_tray(app)?;
            label_shortcuts::setup_label_shortcuts(app)?;

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_health,
            commands::get_latest_prediction,
            commands::get_prediction_history,
            commands::start_session,
            commands::stop_session,
            commands::get_session,
            commands::get_active_session,
            commands::submit_label,
            commands::get_session_recap,
            commands::set_focus_mode,
            commands::dismiss_snapback,
            commands::reload_classifier_model,
            commands::refresh_permissions,
            commands::get_app_rules,
            commands::upsert_app_rule,
            commands::delete_app_rule,
            commands::get_context_timeline,
            commands::export_training_data,
            commands::get_training_deploy_status,
            commands::set_training_repo_path,
            commands::train_from_export,
        ])
        .build(tauri::generate_context!());
    let app = match app {
        Ok(app) => app,
        Err(err) => {
            // panic = "abort" in the release profile turns a plain .expect()
            // here into a silent crash with no message on Windows. Logging
            // first gives users something to report instead of nothing.
            log::error!("Snapback failed to start: {err}");
            eprintln!("Snapback failed to start: {err}");
            std::process::exit(1);
        }
    };

    let handle = app.handle().clone();
    app.run(move |_app_handle, event| {
        if let tauri::RunEvent::Ready = event {
            if let Some(t0) = PROCESS_START.get() {
                log::info!("startup_ms_to_ready={}", t0.elapsed().as_millis());
            }
            crate::events::emit_or_log(&handle, "snapback://ready", ());
        }
    });
}
