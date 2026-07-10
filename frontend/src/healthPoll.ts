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
