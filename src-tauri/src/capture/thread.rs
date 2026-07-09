use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::SyncSender;
use std::sync::{Arc, RwLock};
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use rdev::{Event, EventType, Key};

use crate::capture::active_window::get_active_window_info;
use crate::types::{CaptureEvent, EventType as AppEventType};

/// No keyboard/mouse input for this long → emit `IdleStart`.
const IDLE_THRESHOLD: Duration = Duration::from_secs(5);
const MOUSE_SAMPLE_INTERVAL: Duration = Duration::from_millis(50);

/// Bound on in-flight capture events. At worst-case mouse-move sampling
/// (~20/s) this absorbs several minutes of a stalled engine loop before
/// anything is dropped, while keeping memory bounded if the loop ever wedges.
pub const CAPTURE_CHANNEL_CAPACITY: usize = 4096;

/// Send an event without blocking; if the channel is full, count the drop
/// instead of discarding it invisibly. A full channel means the engine loop
/// is stalled, and capture events are inherently lossy in that case anyway —
/// what matters is that the drop is observable via `dropped`.
fn send_event(tx: &SyncSender<CaptureEvent>, dropped: &AtomicU64, event: CaptureEvent) {
    if tx.try_send(event).is_err() {
        dropped.fetch_add(1, Ordering::Relaxed);
    }
}

pub struct CaptureHandle {
    stop_requested: Arc<AtomicBool>,
    threads: Vec<thread::JoinHandle<()>>,
}

impl CaptureHandle {
    pub fn stop_and_join(self) {
        self.stop_requested.store(true, Ordering::Relaxed);
        for thread in self.threads {
            let _ = thread.join();
        }
    }
}

pub fn start_capture_thread(
    event_tx: SyncSender<CaptureEvent>,
    failure_tx: std::sync::mpsc::Sender<String>,
    dropped_events: Arc<AtomicU64>,
) -> CaptureHandle {
    let stop_requested = Arc::new(AtomicBool::new(false));
    let last_window: Arc<RwLock<Option<(String, String)>>> = Arc::new(RwLock::new(None));
    let last_activity: Arc<RwLock<SystemTime>> = Arc::new(RwLock::new(SystemTime::now()));
    let is_idle: Arc<RwLock<bool>> = Arc::new(RwLock::new(false));
    let idle_started_at: Arc<RwLock<Option<SystemTime>>> = Arc::new(RwLock::new(None));
    let last_mouse: Arc<RwLock<Option<(i32, i32, f64)>>> = Arc::new(RwLock::new(None));
    let last_mouse_sample: Arc<RwLock<SystemTime>> = Arc::new(RwLock::new(SystemTime::now()));

    let idle_thread = start_idle_monitor(
        event_tx.clone(),
        Arc::clone(&dropped_events),
        Arc::clone(&last_window),
        Arc::clone(&last_activity),
        Arc::clone(&is_idle),
        Arc::clone(&idle_started_at),
        Arc::clone(&stop_requested),
    );

    let poll_window = Arc::clone(&last_window);
    let poll_tx = event_tx.clone();
    let poll_dropped = Arc::clone(&dropped_events);
    let poll_stop_requested = Arc::clone(&stop_requested);
    let poll_thread = thread::spawn(move || loop {
        if poll_stop_requested.load(Ordering::Relaxed) {
            break;
        }
        if let Some(info) = get_active_window_info() {
            let mut title_only = false;
            if let Some(current) = poll_window.read().ok().and_then(|g| g.clone()) {
                if current.0 == info.app_name && current.1 == info.window_title {
                    continue;
                }
                title_only = current.0 == info.app_name && current.1 != info.window_title;
            }

            let now = timestamp_secs();
            send_event(
                &poll_tx,
                &poll_dropped,
                CaptureEvent {
                    event_type: if title_only {
                        AppEventType::WindowTitleChange
                    } else {
                        AppEventType::WindowFocusChange
                    },
                    timestamp_secs: now,
                    app_name: info.app_name.clone(),
                    window_title: info.window_title.clone(),
                    mouse_x: 0,
                    mouse_y: 0,
                    mouse_speed: 0,
                    idle_duration_ms: 0,
                },
            );
            if let Ok(mut guard) = poll_window.write() {
                *guard = Some((info.app_name, info.window_title));
            }
        }
        thread::sleep(Duration::from_millis(500));
    });

    let listener_thread = thread::spawn(move || {
        let callback = move |event: Event| {
            let now = timestamp_secs();
            match event.event_type {
                EventType::KeyPress(key) => {
                    if matches!(key, Key::Unknown(_)) {
                        return;
                    }
                    note_activity(
                        &event_tx,
                        &dropped_events,
                        &last_window,
                        &last_activity,
                        &is_idle,
                        &idle_started_at,
                    );
                    let (app, title) = read_window(&last_window);
                    let (mouse_x, mouse_y) = read_last_mouse(&last_mouse);
                    send_event(
                        &event_tx,
                        &dropped_events,
                        CaptureEvent {
                            event_type: AppEventType::KeyPress,
                            timestamp_secs: now,
                            app_name: app,
                            window_title: title,
                            mouse_x,
                            mouse_y,
                            mouse_speed: 0,
                            idle_duration_ms: 0,
                        },
                    );
                }
                EventType::ButtonPress(_) => {
                    note_activity(
                        &event_tx,
                        &dropped_events,
                        &last_window,
                        &last_activity,
                        &is_idle,
                        &idle_started_at,
                    );
                    let (app, title) = read_window(&last_window);
                    let (mouse_x, mouse_y) = read_last_mouse(&last_mouse);
                    send_event(
                        &event_tx,
                        &dropped_events,
                        CaptureEvent {
                            event_type: AppEventType::MouseClick,
                            timestamp_secs: now,
                            app_name: app,
                            window_title: title,
                            mouse_x,
                            mouse_y,
                            mouse_speed: 0,
                            idle_duration_ms: 0,
                        },
                    );
                }
                EventType::MouseMove { x, y } => {
                    let should_sample = last_mouse_sample
                        .read()
                        .ok()
                        .and_then(|t| t.elapsed().ok())
                        .map(|d| d >= MOUSE_SAMPLE_INTERVAL)
                        .unwrap_or(true);
                    if !should_sample {
                        return;
                    }
                    if let Ok(mut sample) = last_mouse_sample.write() {
                        *sample = SystemTime::now();
                    }

                    note_activity(
                        &event_tx,
                        &dropped_events,
                        &last_window,
                        &last_activity,
                        &is_idle,
                        &idle_started_at,
                    );

                    let ix = x as i32;
                    let iy = y as i32;
                    let prev_mouse = last_mouse.read().ok().and_then(|g| *g);
                    let speed = mouse_speed_px_per_sec(prev_mouse, ix, iy, now);
                    if let Ok(mut guard) = last_mouse.write() {
                        *guard = Some((ix, iy, now));
                    }

                    let (app, title) = read_window(&last_window);
                    send_event(
                        &event_tx,
                        &dropped_events,
                        CaptureEvent {
                            event_type: AppEventType::MouseMove,
                            timestamp_secs: now,
                            app_name: app,
                            window_title: title,
                            mouse_x: ix,
                            mouse_y: iy,
                            mouse_speed: speed,
                            idle_duration_ms: 0,
                        },
                    );
                }
                _ => {}
            }
        };

        if let Err(err) = rdev::listen(callback) {
            log::error!("capture thread stopped: {err:?}");
            let _ = failure_tx.send(format!("{err:?}"));
        }
    });

    CaptureHandle {
        stop_requested,
        threads: vec![idle_thread, poll_thread, listener_thread],
    }
}

/// Polls for gaps in input activity and emits idle boundary events.
fn start_idle_monitor(
    event_tx: SyncSender<CaptureEvent>,
    dropped_events: Arc<AtomicU64>,
    last_window: Arc<RwLock<Option<(String, String)>>>,
    last_activity: Arc<RwLock<SystemTime>>,
    is_idle: Arc<RwLock<bool>>,
    idle_started_at: Arc<RwLock<Option<SystemTime>>>,
    stop_requested: Arc<AtomicBool>,
) -> thread::JoinHandle<()> {
    thread::spawn(move || loop {
        if stop_requested.load(Ordering::Relaxed) {
            break;
        }
        thread::sleep(Duration::from_millis(500));

        let currently_idle = is_idle.read().ok().is_some_and(|g| *g);
        if currently_idle {
            continue;
        }

        let now = SystemTime::now();
        let elapsed = last_activity
            .read()
            .ok()
            .and_then(|t| now.duration_since(*t).ok())
            .unwrap_or(Duration::ZERO);

        if elapsed < IDLE_THRESHOLD {
            continue;
        }

        if let Ok(mut guard) = is_idle.write() {
            *guard = true;
        }
        if let Ok(mut guard) = idle_started_at.write() {
            *guard = Some(now);
        }

        let (app, title) = read_window(&last_window);
        send_event(
            &event_tx,
            &dropped_events,
            CaptureEvent {
                event_type: AppEventType::IdleStart,
                timestamp_secs: timestamp_secs(),
                app_name: app,
                window_title: title,
                mouse_x: 0,
                mouse_y: 0,
                mouse_speed: 0,
                idle_duration_ms: 0,
            },
        );
    })
}

/// On resume from idle, emit `IdleEnd` with the full idle span before the activity event.
fn note_activity(
    event_tx: &SyncSender<CaptureEvent>,
    dropped_events: &Arc<AtomicU64>,
    last_window: &Arc<RwLock<Option<(String, String)>>>,
    last_activity: &Arc<RwLock<SystemTime>>,
    is_idle: &Arc<RwLock<bool>>,
    idle_started_at: &Arc<RwLock<Option<SystemTime>>>,
) {
    let now = SystemTime::now();
    let was_idle = is_idle.read().ok().is_some_and(|g| *g);
    if was_idle {
        let duration_ms = idle_started_at
            .read()
            .ok()
            .and_then(|g| *g)
            .and_then(|start| now.duration_since(start).ok())
            .map(|d| d.as_millis() as u32)
            .unwrap_or(0);

        let (app, title) = read_window(last_window);
        send_event(
            event_tx,
            dropped_events,
            CaptureEvent {
                event_type: AppEventType::IdleEnd,
                timestamp_secs: timestamp_secs(),
                app_name: app,
                window_title: title,
                mouse_x: 0,
                mouse_y: 0,
                mouse_speed: 0,
                idle_duration_ms: duration_ms,
            },
        );

        if let Ok(mut guard) = is_idle.write() {
            *guard = false;
        }
        if let Ok(mut guard) = idle_started_at.write() {
            *guard = None;
        }
    }

    if let Ok(mut guard) = last_activity.write() {
        *guard = now;
    }
}

fn mouse_speed_px_per_sec(prev: Option<(i32, i32, f64)>, x: i32, y: i32, now: f64) -> u32 {
    let Some((px, py, pts)) = prev else {
        return 0;
    };
    let dt = (now - pts).max(1e-6);
    let dx = (x - px) as f64;
    let dy = (y - py) as f64;
    let distance = (dx * dx + dy * dy).sqrt();
    (distance / dt).round() as u32
}

fn read_window(last_window: &Arc<RwLock<Option<(String, String)>>>) -> (String, String) {
    last_window
        .read()
        .ok()
        .and_then(|g| g.clone())
        .unwrap_or_else(|| ("Unknown".to_string(), String::new()))
}

/// Last sampled cursor position, or `(0, 0)` before any `MouseMove` has been seen.
fn read_last_mouse(last_mouse: &Arc<RwLock<Option<(i32, i32, f64)>>>) -> (i32, i32) {
    last_mouse
        .read()
        .ok()
        .and_then(|g| *g)
        .map(|(x, y, _)| (x, y))
        .unwrap_or((0, 0))
}

fn timestamp_secs() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

#[cfg(test)]
mod tests {
    use super::{mouse_speed_px_per_sec, CaptureHandle};
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::mpsc::channel;
    use std::sync::Arc;
    use std::thread;
    use std::time::Duration;

    fn dummy_event() -> crate::types::CaptureEvent {
        crate::types::CaptureEvent {
            event_type: crate::types::EventType::KeyPress,
            timestamp_secs: 0.0,
            app_name: String::new(),
            window_title: String::new(),
            mouse_x: 0,
            mouse_y: 0,
            mouse_speed: 0,
            idle_duration_ms: 0,
        }
    }

    #[test]
    fn send_event_counts_drops_without_blocking_when_channel_is_full() {
        use super::send_event;
        use std::sync::atomic::AtomicU64;

        let (tx, rx) = std::sync::mpsc::sync_channel(1);
        let dropped = AtomicU64::new(0);

        send_event(&tx, &dropped, dummy_event());
        // Channel capacity is 1 and it's now full; this must return
        // immediately (not block) and count the drop instead.
        send_event(&tx, &dropped, dummy_event());

        assert_eq!(dropped.load(Ordering::Relaxed), 1);
        assert_eq!(rx.try_iter().count(), 1, "the first event was still delivered");
    }

    #[test]
    fn read_last_mouse_defaults_to_origin_before_any_move() {
        use super::read_last_mouse;
        use std::sync::RwLock;

        let last_mouse: Arc<RwLock<Option<(i32, i32, f64)>>> = Arc::new(RwLock::new(None));
        assert_eq!(read_last_mouse(&last_mouse), (0, 0));
    }

    #[test]
    fn read_last_mouse_returns_last_sampled_position() {
        use super::read_last_mouse;
        use std::sync::RwLock;

        let last_mouse: Arc<RwLock<Option<(i32, i32, f64)>>> =
            Arc::new(RwLock::new(Some((42, -7, 123.0))));
        assert_eq!(read_last_mouse(&last_mouse), (42, -7));
    }

    #[test]
    fn mouse_speed_is_zero_without_prior_sample() {
        assert_eq!(mouse_speed_px_per_sec(None, 10, 10, 1.0), 0);
    }

    #[test]
    fn mouse_speed_computes_pixels_per_second() {
        // 100px in 0.1s → 1000 px/s
        let prev = Some((0, 0, 0.0));
        assert_eq!(mouse_speed_px_per_sec(prev, 100, 0, 0.1), 1000);
    }

    #[test]
    fn capture_handle_stop_and_join_stops_worker_threads() {
        let stop_requested = Arc::new(AtomicBool::new(false));
        let worker_stop = Arc::clone(&stop_requested);
        let (tx, rx) = channel();
        let worker = thread::spawn(move || {
            while !worker_stop.load(Ordering::Relaxed) {
                thread::sleep(Duration::from_millis(5));
            }
            let _ = tx.send(());
        });

        let handle = CaptureHandle {
            stop_requested,
            threads: vec![worker],
        };
        handle.stop_and_join();

        assert!(rx.recv_timeout(Duration::from_secs(1)).is_ok());
    }
}
