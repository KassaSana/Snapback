import { cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { AppRuleRecord, ContextSnapshot } from "../src/api";
import { WorkAppTeachCard } from "../src/WorkAppTeachCard";

afterEach(() => cleanup());

const snapshot = (appName: string, windowTitle = "title"): ContextSnapshot => ({
  appName,
  windowTitle,
  fileHint: "",
  projectHint: "",
  summary: windowTitle,
  timestampMs: Date.parse("2026-08-28T10:00:00Z"),
});

const rule = (pattern: string): AppRuleRecord => ({
  id: 1,
  pattern,
  ruleType: "allow",
  note: null,
  createdAtMs: 0,
  updatedAtMs: 0,
});

describe("WorkAppTeachCard", () => {
  it("asks about unlabeled apps from the session timeline", () => {
    const onCreate = vi.fn();
    render(
      <WorkAppTeachCard
        appRules={[]}
        contextTimeline={[snapshot("Cursor", "workAppTeach.ts"), snapshot("Discord")]}
        dismissed={false}
        onCreateAppRule={onCreate}
        onDismiss={vi.fn()}
      />,
    );

    expect(screen.getByText("Which of these were the work?")).toBeInTheDocument();
    expect(screen.getByText("Cursor")).toBeInTheDocument();
    expect(screen.getByText("Discord")).toBeInTheDocument();

    fireEvent.click(screen.getAllByRole("button", { name: "The work" })[0]);
    expect(onCreate).toHaveBeenCalledWith("Cursor", "allow");
  });

  it("does not render when every app already has a rule or the card was dismissed", () => {
    const { rerender } = render(
      <WorkAppTeachCard
        appRules={[rule("cursor")]}
        contextTimeline={[snapshot("Cursor")]}
        dismissed={false}
        onCreateAppRule={vi.fn()}
        onDismiss={vi.fn()}
      />,
    );
    expect(screen.queryByText("Which of these were the work?")).not.toBeInTheDocument();

    rerender(
      <WorkAppTeachCard
        appRules={[]}
        contextTimeline={[snapshot("Discord")]}
        dismissed
        onCreateAppRule={vi.fn()}
        onDismiss={vi.fn()}
      />,
    );
    expect(screen.queryByText("Which of these were the work?")).not.toBeInTheDocument();
  });

  it("records a block and lets one app be skipped without writing a rule", () => {
    const onCreate = vi.fn();
    const onDismiss = vi.fn();
    render(
      <WorkAppTeachCard
        appRules={[]}
        contextTimeline={[snapshot("YouTube"), snapshot("Code")]}
        dismissed={false}
        onCreateAppRule={onCreate}
        onDismiss={onDismiss}
      />,
    );

    const youtube = screen.getByText("YouTube").closest("li") as HTMLElement;
    fireEvent.click(within(youtube).getByRole("button", { name: "Not the work" }));
    expect(onCreate).toHaveBeenCalledWith("YouTube", "block");

    const code = screen.getByText("Code").closest("li") as HTMLElement;
    fireEvent.click(within(code).getByRole("button", { name: "Skip" }));
    expect(screen.queryByText("Code")).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: /do this later/i }));
    expect(onDismiss).toHaveBeenCalledOnce();
  });
});
