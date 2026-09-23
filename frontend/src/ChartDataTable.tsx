// Roadmap 10.3. The Review charts are `<svg role="img">`, which is right for the picture and
// hides every bar's `<title>` from assistive tech; nothing in them takes keyboard focus either.
// So a screen-reader or keyboard user got each chart's name and none of its numbers. This is
// the same data as a table, behind a native disclosure: reachable by Tab, announced as a table,
// closed by default so the cards look as they did. Rows come from the arrays the bars are
// drawn from, so the table cannot say something the chart does not.
import { memo } from "react";

export type ChartDataRow = { key: string; name: string; detail: string };

type ChartDataTableProps = {
  /** What the table is, read out as its caption. */
  caption: string;
  /** Header for the first column: "Hour", "Day", "Session". */
  nameHeader: string;
  rows: readonly ChartDataRow[];
};

export const ChartDataTable = memo(function ChartDataTable({
  caption,
  nameHeader,
  rows,
}: ChartDataTableProps) {
  return (
    <details className="chart-data">
      <summary>Show data</summary>
      <table>
        <caption>{caption}</caption>
        <thead>
          <tr>
            <th scope="col">{nameHeader}</th>
            <th scope="col">Values</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((row) => (
            <tr key={row.key}>
              <th scope="row">{row.name}</th>
              <td>{row.detail}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </details>
  );
});
