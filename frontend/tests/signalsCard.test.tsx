import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { SignalsCard } from "../src/SignalsCard";

afterEach(() => cleanup());

describe("SignalsCard", () => {
  it("renders every rolling classifier signal in order", () => {
    render(<SignalsCard signals={["thrash 12%", "drift 4%", "goal fit 81%"]} />);

    expect(screen.getByRole("heading", { name: "Signals" })).toBeInTheDocument();
    expect(screen.getAllByRole("listitem").map((item) => item.textContent)).toEqual([
      "thrash 12%",
      "drift 4%",
      "goal fit 81%",
    ]);
    expect(screen.getByText("rolling 30s")).toBeInTheDocument();
  });

  it("keeps an empty semantic list while waiting for signals", () => {
    render(<SignalsCard signals={[]} />);

    expect(screen.getByRole("list")).toBeEmptyDOMElement();
  });
});
