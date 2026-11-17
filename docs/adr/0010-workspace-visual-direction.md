# ADR-0010: A workspace look instead of cream and cards

- **Status:** Accepted
- **Date:** 2026-11-17
- **Supersedes:** the visual-direction paragraph of [ADR-0003](0003-three-surface-dashboard.md)

## Question

ADR-0003 kept the cream background, serif headings, rounded cards and coral accent so the
reorganisation could be judged on its own. The reorganisation held up. Should the look change
now?

## Context

- The palette (cream `#f8f6f0`, terracotta `#c65d44`, serif display) is the stock look of
  generated web pages right now. People read Snapback as a template before they read it as a
  tool.
- Every block was a bordered, shadowed card with a pill badge, so nothing stood out. The running
  goal and the drift prompt looked the same as a settings block.
- Snapback runs in a fixed 1100 x 760 desktop window next to the work it watches. It should feel
  like a quiet utility, not a page.

## Decision

- **Layout:** a left rail with the three surfaces and, at the bottom, the recording status and
  pause controls on every surface. Content is a single left-aligned column.
- **Now:** the running goal is the page title. A thin session ribbon above it shows one segment
  per recent reading retained by the interface. The verdict is one quiet line. A snapback shows as a tinted row with
  "Take me back".
- **Structure:** hairlines between sections instead of cards. Raised surfaces only for things that
  float (menus, dialogs).
- **Colour:** cool neutrals, one accent (pine `#17695a`) meaning "on task" and one signal colour
  (ochre `#d9962f`) meaning drift. Dark theme uses the same roles.
- **Type:** IBM Plex Sans, bundled locally with a system fallback, at desktop sizes: 13–14px
  body, 22px titles, 28px for the running goal and timer. No serif, no uppercase labels.

## Why

The goal and the drift prompt are the only two things Now has to get across, and the old look
gave them the same weight as everything else. Hairlines and one accent let them stand out
without adding more decoration.

The Latin font subset is bundled and embedded in the single-file build, so typography requires no
network access. Other characters use the platform fallback. Its open-font license is included.

## Consequences

- Review starts with the session explorer and summary, with charts under Details. Range labels
  remain visible so stale data cannot be mistaken for the newly selected interval.
- Settings uses a compact section navigation. The native overlays use dark neutral and ochre
  colours to match the workspace.
- Tests are unaffected by the look: they query roles and text, not classes.
