import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { FocusFeedbackCard } from "../src/FocusFeedbackCard";

afterEach(() => {
  cleanup();
});

describe("FocusFeedbackCard", () => {
  it("forwards label clicks and shows status", () => {
    const handleLabel = vi.fn();
    render(
      <FocusFeedbackCard
        sessionActive
        handleLabel={handleLabel}
        labelStatus="Saved."
        labelStatusWarning={false}
      />,
    );

    expect(screen.getByText(/label moments/i)).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Deep" }));
    expect(handleLabel).toHaveBeenCalledWith("DEEP_FOCUS");
    expect(screen.getByText("Saved.")).toBeInTheDocument();
  });

  it("marks warning status", () => {
    render(
      <FocusFeedbackCard
        sessionActive
        handleLabel={() => undefined}
        labelStatus="Could not save."
        labelStatusWarning
      />,
    );
    expect(screen.getByText("Could not save.")).toHaveClass("alert");
  });
});

it("disables live feedback without an active session", () => {
  const handleLabel = vi.fn();
  render(<FocusFeedbackCard sessionActive={false} handleLabel={handleLabel} labelStatus={null} labelStatusWarning={false} />);
  for (const button of screen.getAllByRole("button")) {
    expect(button).toBeDisabled();
    fireEvent.click(button);
  }
  expect(handleLabel).not.toHaveBeenCalled();
  expect(screen.getByText("Start a session to save live feedback.")).toBeInTheDocument();
});
