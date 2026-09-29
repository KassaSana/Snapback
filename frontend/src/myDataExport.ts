// What to say after "Export my data": whether the user holds all of their data, naming any
// record types that were omitted.

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
  if (result.omittedEpisodes > 0) omissions.push(plural(result.omittedEpisodes, "interruption"));
  return `Wrote ${contents} to ${result.outputPath}. ${omissions.join(" and ")} could not be included.`;
}
