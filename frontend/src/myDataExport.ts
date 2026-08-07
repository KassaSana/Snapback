// Roadmap 9.16. What to say after "Export my data".
//
// Pure and separately tested for the same reason activityDeletion.ts is: this sentence is a
// claim about whether the user is holding all of their data, and the component suite cannot
// run on this machine (11.11).
//
// The claim it replaces was false in two ways at once. The document said it contained "every
// session" while the command stopped at 200 of them and 500 windows within each; and the
// `truncated` flag came from the session cap alone, so a file that dropped the 501st window of
// an *included* session was reported to the UI as complete. The export is now complete, and
// the wording says which of those two things happened rather than leaving it implied.

import type { MyDataExportResult } from "./api";

const plural = (count: number, noun: string): string =>
  `${count} ${noun}${count === 1 ? "" : "s"}`;

/** The status line the Privacy card shows after an export. */
export function myDataExportMessage(result: MyDataExportResult): string {
  const contents = [
    plural(result.sessionCount, "session"),
    plural(result.windowCount, "captured window"),
    plural(result.episodeCount, "interruption"),
  ].join(", ");

  if (!result.truncated) {
    // Said explicitly. "Complete" is the whole point of the feature, and a message that only
    // reports counts leaves the reader to wonder what is missing.
    return `Wrote ${contents} to ${result.outputPath}. This is your complete history — nothing was left out.`;
  }

  // Named per record type, because "some data was omitted" is not something a user can act on.
  const omissions: string[] = [];
  if (result.omittedSessions > 0) omissions.push(plural(result.omittedSessions, "session"));
  if (result.omittedWindows > 0) omissions.push(plural(result.omittedWindows, "captured window"));
  return `Wrote ${contents} to ${result.outputPath}. ${omissions.join(" and ")} could not be included.`;
}

/** Whether the message above is reporting an incomplete archive. */
export function myDataExportIsIncomplete(result: MyDataExportResult): boolean {
  return result.truncated;
}
