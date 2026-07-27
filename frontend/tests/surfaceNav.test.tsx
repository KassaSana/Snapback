import { useState } from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import {
  SurfaceNav,
  surfacePanelId,
  surfaceTabId,
  type Surface,
} from "../src/SurfaceNav";

afterEach(() => cleanup());

function NavigationHarness({ initial = "now" }: { initial?: Surface }) {
  const [active, setActive] = useState<Surface>(initial);
  return <SurfaceNav active={active} onChange={setActive} />;
}

describe("SurfaceNav", () => {
  it("selects a surface and exposes the matching panel relationship", () => {
    render(<NavigationHarness />);

    const review = screen.getByRole("tab", { name: "Review" });
    fireEvent.click(review);

    expect(review).toHaveAttribute("aria-selected", "true");
    expect(review).toHaveAttribute("id", surfaceTabId("review"));
    expect(review).toHaveAttribute("aria-controls", surfacePanelId("review"));
    expect(screen.getByRole("tab", { name: "Now" })).toHaveAttribute("tabindex", "-1");
  });

  it("moves and wraps focus with the standard tab keyboard controls", () => {
    render(<NavigationHarness />);

    const now = screen.getByRole("tab", { name: "Now" });
    now.focus();
    fireEvent.keyDown(now, { key: "ArrowLeft" });
    expect(screen.getByRole("tab", { name: "Settings" })).toHaveFocus();

    fireEvent.keyDown(screen.getByRole("tab", { name: "Settings" }), { key: "Home" });
    expect(screen.getByRole("tab", { name: "Now" })).toHaveFocus();

    fireEvent.keyDown(screen.getByRole("tab", { name: "Now" }), { key: "End" });
    expect(screen.getByRole("tab", { name: "Settings" })).toHaveFocus();
  });
});
