// The chart's data as a table behind a native disclosure, since `<svg role="img">` hides every
// bar from assistive tech. Rows come from the same arrays the bars do.
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
