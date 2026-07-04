import unittest

from ml.event_schema import EventRecord, EventType
from ml.features import FeatureExtractor, MOUSE_MOVE_STRUCT, IDLE_STRUCT, _classify_app


def make_event(event_type: EventType, ts_us: int, app_name: str, data_raw: bytes = b"") -> EventRecord:
    data = data_raw.ljust(16, b"\x00")
    return EventRecord(
        timestamp_us=ts_us,
        event_type=event_type,
        process_id=4242,
        app_name=app_name,
        window_handle=0,
        data_raw=data,
        reserved=0,
    )


class TestFeatureExtractor(unittest.TestCase):
    def test_keystroke_rate_and_intervals(self) -> None:
        extractor = FeatureExtractor(window_seconds=30)
        events = [
            make_event(EventType.KEY_PRESS, 0, "code.exe"),
            make_event(EventType.KEY_PRESS, 500_000, "code.exe"),
            make_event(EventType.KEY_PRESS, 1_000_000, "code.exe"),
        ]
        for event in events:
            features = extractor.update(event)

        self.assertEqual(features.keystroke_count, 3)
        self.assertAlmostEqual(features.keystroke_interval_mean, 0.5, places=3)
        self.assertTrue(features.is_ide)

    def test_context_switches_and_apps(self) -> None:
        extractor = FeatureExtractor(window_seconds=30, long_window_seconds=300)
        events = [
            make_event(EventType.WINDOW_FOCUS_CHANGE, 0, "code.exe"),
            make_event(EventType.KEY_PRESS, 1_000_000, "code.exe"),
            make_event(EventType.WINDOW_FOCUS_CHANGE, 2_000_000, "chrome.exe"),
            make_event(EventType.KEY_PRESS, 3_000_000, "chrome.exe"),
        ]
        for event in events:
            features = extractor.update(event)

        self.assertEqual(features.context_switches_30s, 2)
        self.assertEqual(features.unique_apps_5min, 2)
        self.assertTrue(features.is_browser)

    def test_mouse_speed_and_idle(self) -> None:
        extractor = FeatureExtractor(window_seconds=30)
        move1 = MOUSE_MOVE_STRUCT.pack(0, 0, 100)
        move2 = MOUSE_MOVE_STRUCT.pack(10, 0, 300)
        idle = IDLE_STRUCT.pack(10_000)
        events = [
            make_event(EventType.MOUSE_MOVE, 0, "code.exe", move1),
            make_event(EventType.MOUSE_MOVE, 1_000_000, "code.exe", move2),
            make_event(EventType.IDLE_END, 2_000_000, "code.exe", idle),
        ]
        for event in events:
            features = extractor.update(event)

        self.assertEqual(features.mouse_move_count, 2)
        self.assertGreater(features.mouse_speed_mean, 0.0)
        self.assertAlmostEqual(features.idle_time_30s, 10.0, places=2)

    def test_classify_app_matches_rust_app_context_for_macos_names(self) -> None:
        # These app names mirror src-tauri/src/engine/app_context.rs and its
        # fixtures/feature_parity/scenarios.json fixtures. Python must agree
        # with Rust's substring/case-insensitive classification, not just the
        # legacy Windows .exe names.
        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app("Cursor")
        self.assertTrue(is_ide)
        self.assertFalse(is_browser)

        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app("Google Chrome")
        self.assertTrue(is_browser)
        self.assertFalse(is_ide)

        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app("Safari")
        self.assertTrue(is_browser)

        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app("Slack")
        self.assertTrue(is_comm)

        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app("Spotify")
        self.assertTrue(is_ent)

        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app("Notion")
        self.assertTrue(is_prod)

    def test_extractor_classifies_macos_app_names(self) -> None:
        extractor = FeatureExtractor(window_seconds=30)
        events = [
            make_event(EventType.WINDOW_FOCUS_CHANGE, 0, "Cursor"),
            make_event(EventType.KEY_PRESS, 400_000, "Cursor"),
        ]
        for event in events:
            features = extractor.update(event)

        self.assertTrue(features.is_ide)
        self.assertEqual(features.productivity_category, "Building")


if __name__ == "__main__":
    unittest.main()
