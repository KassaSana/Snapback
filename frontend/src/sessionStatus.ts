import type { SessionRecord } from "./api";

/**
 * The label shown for the current session (Roadmap 7.23 / ADR-0005).
 *
 * A session is now **running or paused**, and the difference is not cosmetic: idle pauses the
 * session, and a paused one accrues no attended time. Showing "active" for a session that
 * stopped counting twenty minutes ago is exactly the confusion 7.23 exists to remove — a user
 * who walked away and came back needs to be able to tell, at a glance, whether the last hour
 * was recorded.
 *
 * A pure function rather than logic inline in App so it can be tested by the `tsx tests/*.ts`
 * runner, which works on any Node. The component suite currently cannot run on the dev
 * machine at all (Roadmap 11.11), so anything only reachable through a rendered component is
 * effectively untested locally.
 *
 * `userIdle` is the engine's own idle signal, which it has emitted since idle detection
 * landed and which nothing consumed until now.
 */
export function sessionStatusLabel(
  record: SessionRecord | null,
  userIdle: boolean,
): string {
  // No session at all. Deliberately not "idle": that word already means "the user is away",
  // and the two states are independent — you can be present with no session, or absent with
  // one running.
  if (!record) return "no session";

  // Anything already finished keeps its own status; only a live session can be paused.
  if (record.status !== "ACTIVE") return record.status.toLowerCase();

  return userIdle ? "paused" : "running";
}
