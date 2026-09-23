import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { PermissionWizard } from "../src/PermissionWizard";

// Roadmap 10.3. The wizard declares `aria-modal`; these pin the behaviour that declaration
// promises. Rendered directly rather than through App so each property is one assertion.

type Props = Parameters<typeof PermissionWizard>[0];

const baseProps = (overrides: Partial<Props> = {}): Props => ({
  healthChecked: true,
  captureProbeConfirmed: false,
  captureRunning: false,
  permissionMessage: null,
  permissionSteps: ["Open System Settings > Privacy & Security > Accessibility."],
  onRefreshPermissions: () => {},
  onRequestPermissions: () => {},
  focusMode: "normal",
  onFocusModeChange: () => {},
  ...overrides,
});

// The wizard is a sibling of the page in App; model that so "the page behind" exists.
const Page = (props: Props) => (
  <div>
    <PermissionWizard {...props} />
    <button type="button">Page control</button>
  </div>
);

describe("PermissionWizard as a modal", () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => {
    cleanup();
    localStorage.clear();
  });

  it("moves focus to the primary action when it opens", () => {
    render(<Page {...baseProps()} />);
    expect(document.activeElement).toBe(screen.getByRole("button", { name: "Grant access" }));
  });

  it("falls back to Check again when there is nothing to grant", () => {
    render(<Page {...baseProps({ permissionSteps: [] })} />);
    expect(document.activeElement).toBe(screen.getByRole("button", { name: "Check again" }));
  });

  it("wraps Tab and Shift+Tab inside the dialog", () => {
    render(<Page {...baseProps()} />);
    const dialog = screen.getByRole("dialog");
    const modeSelect = screen.getByRole("combobox");
    const skip = screen.getByRole("button", { name: "Skip for now" });

    skip.focus();
    fireEvent.keyDown(dialog, { key: "Tab" });
    expect(document.activeElement).toBe(modeSelect);

    modeSelect.focus();
    fireEvent.keyDown(dialog, { key: "Tab", shiftKey: true });
    expect(document.activeElement).toBe(skip);
  });

  it("makes the page behind inert while open, and only while open", () => {
    render(<Page {...baseProps()} />);
    const pageControl = screen.getByRole("button", { name: "Page control", hidden: true });
    expect(pageControl.inert).toBe(true);

    fireEvent.click(screen.getByRole("button", { name: "Skip for now" }));
    expect(screen.queryByRole("dialog")).toBeNull();
    expect(pageControl.inert).toBe(false);
  });

  it("returns focus to what had it before the dialog opened", () => {
    const hidden = baseProps({ healthChecked: false });
    const { rerender } = render(<Page {...hidden} />);
    const pageControl = screen.getByRole("button", { name: "Page control" });
    pageControl.focus();

    rerender(<Page {...baseProps()} />);
    expect(document.activeElement).toBe(screen.getByRole("button", { name: "Grant access" }));

    fireEvent.click(screen.getByRole("button", { name: "Skip for now" }));
    expect(document.activeElement).toBe(pageControl);
  });
});
