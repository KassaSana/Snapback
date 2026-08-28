// Now surface mode (ADR-0003).
//
// The tab is one job; the *screen* still has three shapes. Idle is a start form, running is
// the session, and just-stopped is the recap of the session that just ended. Deriving that
// here keeps App.tsx from growing a nest of `sessionActive && recap` checks that would drift
// apart from the tests.

export const NOW_SURFACE_MODES = ["idle", "running", "stopped"] as const;

export type NowSurfaceMode = (typeof NOW_SURFACE_MODES)[number];

export type NowSurfaceInput = {
  /** A live session row. Idle-pause still counts: the session is running, the user is away. */
  sessionActive: boolean;
  /** Recap of the session that just ended. Null once a new session starts or none has. */
  recap: unknown | null;
};

/**
 * Which Now layout to show.
 *
 * A running session wins over a leftover recap, so Start cannot strand the user on the
 * previous session's check-in. Stopped is "there is a recap and nothing is running", which
 * is the Stop payoff living on Now rather than behind the Review tab.
 */
export function nowSurfaceMode(input: NowSurfaceInput): NowSurfaceMode {
  if (input.sessionActive) return "running";
  if (input.recap !== null) return "stopped";
  return "idle";
}
