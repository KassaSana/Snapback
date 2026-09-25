import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { VerdictFeedback } from "../src/VerdictFeedback";

afterEach(() => cleanup());

describe("VerdictFeedback", () => {
  it("keeps the correction control quiet until requested", () => {
    render(
      <VerdictFeedback
        disabled={false}
        onCorrect={vi.fn()}
        predictedState="PRODUCTIVE"
        status={null}
      />,
    );
    expect(screen.getByRole("button", { name: "This reading is wrong" })).toHaveTextContent("Wrong?");
    expect(screen.queryByText("What was it really?")).not.toBeInTheDocument();
  });

  it("requires a different concrete label for a correction", () => {
    const onCorrect = vi.fn();
    render(
      <VerdictFeedback
        disabled={false}
        onCorrect={onCorrect}
        predictedState="PRODUCTIVE"
        status="Ready"
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "This reading is wrong" }));
    expect(screen.queryByRole("button", { name: "Productive" })).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Deep work" }));

    expect(onCorrect).toHaveBeenCalledWith("DEEP_FOCUS");
    expect(screen.getByText("Ready")).toHaveAttribute("aria-live", "polite");
  });

  it("offers the same correction choices for an uncertain reading", () => {
    render(
      <VerdictFeedback
        disabled={false}
        onCorrect={vi.fn()}
        predictedState="DEEP_FOCUS"
        status={null}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: "This reading is wrong" }));
    expect(screen.getByRole("button", { name: "Productive" })).toBeInTheDocument();
  });

  it("explains why feedback is unavailable", () => {
    render(
      <VerdictFeedback
        disabled
        onCorrect={vi.fn()}
        predictedState="PRODUCTIVE"
        status={null}
      />,
    );

    expect(screen.queryByText("Start a session to rate this reading.")).not.toBeInTheDocument();
    expect(screen.queryByRole("button")).not.toBeInTheDocument();
  });
});
