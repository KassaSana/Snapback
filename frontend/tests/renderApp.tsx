import { fireEvent, render, screen } from "@testing-library/react";

import App from "../src/App";
import type { Surface } from "../src/SurfaceNav";

// ADR-0003 split the dashboard into Now / Review / Settings, so a card is only mounted
// when its surface is showing. Tests that assert on a Review or Settings card must say so
// — that is the behaviour, not an inconvenience: the old tests passed because everything
// was rendered at once, which was the problem the ADR fixed.
//
// Defaults to "now" so `renderApp()` matches what a user sees on launch.
export function renderApp(surface: Surface = "now") {
  const result = render(<App />);
  if (surface !== "now") {
    // Role-based, so this breaks loudly if the nav stops being a real tablist.
    fireEvent.click(screen.getByRole("tab", { name: surfaceLabel(surface) }));
  }
  return result;
}

function surfaceLabel(surface: Surface): string {
  return surface === "review" ? "Review" : "Settings";
}
