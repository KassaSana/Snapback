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

// ---------------------------------------------------------------------------
// Settings surface. Roadmap 2.16.
//
// These live here rather than in SettingsCard for the reason at the top of the file: the
// component suite cannot be relied on to run, so anything with a rule in it belongs where
// `tsx` can execute it.
// ---------------------------------------------------------------------------

export const ALERT_CHANNELS = ["inApp", "overlay", "native"] as const;
export type AlertChannel = (typeof ALERT_CHANNELS)[number];

export const ALERT_CHANNEL_LABELS: Record<AlertChannel, string> = {
  inApp: "In the app",
  overlay: "Overlay card",
  native: "System notification",
};

export const ALERT_EVENTS = ["snapback", "hyperfocus", "pomodoro"] as const;
export type AlertEventKey = (typeof ALERT_EVENTS)[number];

export const ALERT_EVENT_LABELS: Record<AlertEventKey, string> = {
  snapback: "Coming back from a distraction",
  hyperfocus: "Time for a break",
  pomodoro: "Pomodoro phase changes",
};

export type AlertDeliverySettings = {
  snapback: AlertChannel[];
  hyperfocus: AlertChannel[];
  pomodoro: AlertChannel[];
  preview: AlertPreviewMode;
  quietHoursEnabled: boolean;
  quietHoursStartMin: number;
  quietHoursEndMin: number;
  snoozedUntilWallMs: number;
};

export const MINUTES_PER_DAY = 1440;

/**
 * "22:30" -> 1350. Null for anything that is not a real minute of the day.
 *
 * Rejects rather than coerces, because every wrong answer here is a quiet range the user did
 * not choose and cannot see. "7" is not 07:00 — a lone number is as likely a typo mid-edit.
 */
export function parseTimeOfDay(value: string): number | null {
  const match = /^(\d{1,2}):(\d{2})$/.exec(value.trim());
  if (!match) return null;
  const hours = Number(match[1]);
  const minutes = Number(match[2]);
  if (hours > 23 || minutes > 59) return null;
  return hours * 60 + minutes;
}

/** 1350 -> "22:30". Zero-padded so the field stays a fixed width as it is edited. */
export function formatTimeOfDay(minute: number): string {
  const wrapped = ((Math.trunc(minute) % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY;
  const hours = Math.floor(wrapped / 60);
  const minutes = wrapped % 60;
  return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}`;
}

/**
 * What the quiet-hours row says under its heading.
 *
 * Names the midnight wrap explicitly. A range shown as "22:00 to 07:00" with no further
 * comment reads to plenty of people as fifteen hours of silence starting in the morning.
 */
export function quietHoursSummary(settings: {
  quietHoursEnabled: boolean;
  quietHoursStartMin: number;
  quietHoursEndMin: number;
}): string {
  if (!settings.quietHoursEnabled) return "Alerts can arrive at any time.";
  const start = settings.quietHoursStartMin;
  const end = settings.quietHoursEndMin;
  // Matches minute_in_quiet_range in src/app/alert_routing.hpp: an empty range, not a full day.
  if (start === end) return "Start and end are the same, so nothing is silenced.";
  const wraps = start > end;
  return `Silent from ${formatTimeOfDay(start)} to ${formatTimeOfDay(end)}${
    wraps ? " the next day." : "."
  }`;
}

/** One sentence naming where an event will show up, or that it will not. */
export function routingSummary(channels: AlertChannel[]): string {
  const named = ALERT_CHANNELS.filter((c) => channels.includes(c)).map(
    (c) => ALERT_CHANNEL_LABELS[c],
  );
  if (named.length === 0) return "Will not interrupt you.";
  if (named.length === 1) return `Shows as: ${named[0]}.`;
  return `Shows as: ${named.slice(0, -1).join(", ")} and ${named[named.length - 1]}.`;
}

/**
 * "18 min left". Rounded **up**, so a snooze with 30 seconds to run never reads "0 min left" —
 * a countdown that hits zero while still silencing looks like a bug in the feature.
 */
export function snoozeRemainingLabel(remainingMs: number): string {
  if (remainingMs <= 0) return "";
  const minutes = Math.ceil(remainingMs / 60_000);
  return `${minutes} min left`;
}
