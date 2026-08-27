/**
 * Roadmap 2.16. The frontend half of alert delivery: reading the route the engine attaches to
 * an alert event, and deciding whether the *in-app* surface is one of the channels it names.
 *
 * Kept pure and out of the components for the reason 11.11 makes concrete — the component
 * suite cannot be relied on to run on every machine, so the rules worth testing live in a
 * module `tsx` can execute directly.
 *
 * The engine already decided everything here. Nothing in this file re-derives policy: quiet
 * hours, the snooze deadline, and the channel preferences were all resolved in
 * src/app/alert_routing.hpp before the event was emitted. This only reads flags.
 */

export type AlertPreviewMode = "detailed" | "generic";

export interface AlertRoute {
  inApp: boolean;
  overlay: boolean;
  native: boolean;
  preview: AlertPreviewMode;
}

/**
 * The route an event carries, or a delivering fallback when it carries none.
 *
 * Absent means a bug rather than an old payload — the tick that emits these runs in the same
 * binary that renders them — so the fallback delivers. An all-false default would make a
 * missing field look exactly like a user who asked for silence, and the app would quietly stop
 * interrupting while appearing to work. Fail open here matches the same decision the native
 * side takes, for the same reason.
 */
export function parseAlertRoute(raw: unknown): AlertRoute {
  const source = (raw ?? {}) as Record<string, unknown>;
  const delivery = source.delivery;
  if (delivery === null || delivery === undefined || typeof delivery !== "object") {
    return { inApp: true, overlay: false, native: false, preview: "detailed" };
  }
  const fields = delivery as Record<string, unknown>;
  return {
    inApp: fields.inApp === true,
    overlay: fields.overlay === true,
    native: fields.native === true,
    preview: fields.preview === "generic" ? "generic" : "detailed",
  };
}

/**
 * Whether this event should raise something the user sees *inside the app*.
 *
 * Deliberately narrow. An event can carry state the app must apply whether or not it is
 * allowed to interrupt — a Pomodoro phase change advances the timer card, a snapback refreshes
 * the timeline — and gating those on a delivery preference would freeze the UI for anyone who
 * turned the alert off. Callers gate the *alert* on this and apply the state update
 * unconditionally.
 */
export function deliversInApp(raw: unknown): boolean {
  return parseAlertRoute(raw).inApp;
}
