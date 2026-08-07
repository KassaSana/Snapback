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
        handleLabel={() => undefined}
        labelStatus="Could not save."
        labelStatusWarning
      />,
    );
    expect(screen.getByText("Could not save.")).toHaveClass("alert");
  });
});
