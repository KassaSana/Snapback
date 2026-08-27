/**
 * Roadmap 2.16's action-routing half. Where a click on a native alert lands in the app.
 *
 * The native side decides *which destination* — `alert_action_for` in
 * src/app/alert_routing.hpp — and this side decides what that destination looks like as a
 * screen. The split is deliberate: main.cpp does not know what a React surface is, and
 * teaching it would put one decision in two places that cannot both stay right.
 *
 * Pure, and out of the components, for the reason `alertDelivery.ts` next door gives: 11.11
 * makes the component suite unreliable to run on every machine, so anything with a rule in it
 * belongs where `tsx` can execute it directly.
 */

import type { Surface } from "./SurfaceNav";

/**
 * The destinations the native side can name. These strings are the wire format — they match
 * `alert_action_as_str` in src/app/alert_routing.hpp exactly, spaces included.
 */
export const ALERT_ACTIONS = [
  "return to work",
  "open session composer",
  "open pomodoro",
] as const;
export type AlertAction = (typeof ALERT_ACTIONS)[number];

/**
 * What the app should bring into view.
 *
 * `focus` names a region rather than an element id: the app decides how to draw attention to
 * it, and a destination that outlived the card it was meant to highlight should not be able to
 * throw a query selector at a component that has since been renamed.
 */
export type AlertDestination = {
  surface: Surface;
  focus: "session" | "pomodoro" | null;
};

/**
 * Just bring the window forward and change nothing.
 *
 * The deliberate answer for anything unrecognised. A click always earns the window coming to
 * the front — that much the user unambiguously asked for — but a destination this build does
 * not understand is not a licence to guess at a screen nobody chose.
 */
export const NO_DESTINATION: AlertDestination = { surface: "now", focus: null };

/**
 * Both remaining destinations land on Now, which is not a placeholder: Now is where a session
 * is started and where the Pomodoro card lives. They differ in what they draw attention to,
 * which is why `focus` exists rather than the surface alone being the answer.
 *
 * "return to work" is absent on purpose. That destination is handled natively by
 * `restore_snapback_target` — it raises *another application's* window, so a frontend that
 * navigated for it would be pulling the user back to Snapback at the exact moment the native
 * side was sending them away from it.
 */
const DESTINATIONS: Record<AlertAction, AlertDestination> = {
  "return to work": { surface: "now", focus: null },
  "open session composer": { surface: "now", focus: "session" },
  "open pomodoro": { surface: "now", focus: "pomodoro" },
};

/** Whether a string is a destination this build knows. */
export function isAlertAction(value: unknown): value is AlertAction {
  return typeof value === "string" && (ALERT_ACTIONS as readonly string[]).includes(value);
}

/**
 * The destination named by an `alert_action` payload.
 *
 * Total: every malformed, missing, or newer-than-this-build input answers NO_DESTINATION
 * rather than throwing. This runs inside a host event listener, where an exception has no
 * user-visible failure mode — it just means the click silently did nothing at all.
 */
export function alertDestination(raw: unknown): AlertDestination {
  const source = (raw ?? {}) as Record<string, unknown>;
  const action = source.action;
  if (!isAlertAction(action)) return NO_DESTINATION;
  return DESTINATIONS[action];
}
