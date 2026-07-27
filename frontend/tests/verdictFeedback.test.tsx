import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { VerdictFeedback } from "../src/VerdictFeedback";

afterEach(() => cleanup());

describe("VerdictFeedback", () => {
  it("submits agreement with the displayed prediction", () => {
    const onConfirm = vi.fn();
    render(
      <VerdictFeedback
        disabled={false}
        onConfirm={onConfirm}
        onCorrect={vi.fn()}
        predictedState="PRODUCTIVE"
        status={null}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "This reading is right" }));
    expect(onConfirm).toHaveBeenCalledOnce();
  });

  it("requires a different concrete label for a correction", () => {
    const onCorrect = vi.fn();
    render(
      <VerdictFeedback
        disabled={false}
        onConfirm={vi.fn()}
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

  it("explains why feedback is unavailable", () => {
    render(
      <VerdictFeedback
        disabled
        onConfirm={vi.fn()}
        onCorrect={vi.fn()}
        predictedState="PRODUCTIVE"
        status={null}
      />,
    );

    expect(screen.getByText("Start a session to rate this reading.")).toBeInTheDocument();
    expect(screen.queryByRole("button")).not.toBeInTheDocument();
  });
});
