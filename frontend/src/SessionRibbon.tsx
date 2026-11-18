import { memo, useEffect, useMemo, useState } from "react";

import { formatTime, type PredictionRecord } from "./api";
import {
  RIBBON_KIND_LABELS,
  mergeReadings,
  ribbonKind,
  ribbonSummary,
  type RibbonKind,
} from "./ribbonSegments";

type Props = {
  sessionId: string | null;
  startedAtMs: number | null | undefined;
  goal: string | null;
  /** The latest reading and the recent history; the ribbon keeps its own longer list. */
  prediction: PredictionRecord | null;
  predictionHistory: PredictionRecord[];
};

const LEGEND: RibbonKind[] = ["deep", "productive", "drift", "unknown"];

/** A thin strip of recent readings observed while this session is open. */
export const SessionRibbon = memo(function SessionRibbon({
  sessionId,
  startedAtMs,
  goal,
  prediction,
  predictionHistory,
}: Props) {
  const [readings, setReadings] = useState<PredictionRecord[]>([]);

  useEffect(() => {
    setReadings((current) => mergeReadings(current, [...predictionHistory, prediction], sessionId));
  }, [prediction, predictionHistory, sessionId]);

  const kinds = useMemo(() => readings.map((record) => ribbonKind(record, goal)), [readings, goal]);

  return (
    <div className="session-ribbon">
      <div className="session-ribbon-track" role="img" aria-label={ribbonSummary(kinds)}>
        {readings.map((record, index) => (
          <span
            key={record.timestampMs}
            className={`session-ribbon-segment session-ribbon-${kinds[index]}`}
          />
        ))}
        <span className="session-ribbon-rest" />
      </div>
      <div className="session-ribbon-caption">
        <span>Recent readings · {startedAtMs ? `Started ${formatTime(startedAtMs)}` : "Started just now"}</span>
        {/* The words come from CSS (attr), so the legend never doubles the verdict's text
            for a screen reader or a text search: the track's label already says it. */}
        <span className="session-ribbon-legend" aria-hidden="true">
          {LEGEND.map((kind) => (
            <span
              key={kind}
              className={`session-ribbon-key session-ribbon-key-${kind}`}
              data-label={RIBBON_KIND_LABELS[kind]}
            />
          ))}
        </span>
      </div>
    </div>
  );
});
