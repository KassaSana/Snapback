// Background health polling: keep re-checking health until capture is confirmed
// up, so the app recovers on its own when the user grants a permission after
// launch (macOS Accessibility, a display appearing, the listener coming up)
// instead of waiting for a manual refresh.

/** How often to re-check health while capture isn't confirmed running. */
export const HEALTH_POLL_MS = 5000;

/**
 * Whether to keep polling health. We poll only while the capture listener is
 * not confirmed running: once it's up, live push events (predictions, capture
 * failure) carry state changes, so continuous polling would be wasted work.
 */
export const shouldPollHealth = (captureRunning: boolean): boolean => !captureRunning;

/**
 * After capture starts, re-check health once past the stall grace window so a
 * dead-on-arrival listener (running, but never delivering events) surfaces on
 * its own instead of waiting for a manual refresh. Slightly longer than the
 * backend's NO_EVENTS_GRACE (15s) so the stall flag has settled.
 */
export const CAPTURE_STALL_RECHECK_MS = 16000;
