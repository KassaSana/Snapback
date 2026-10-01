---
name: snapback-grill-design
description: >-
  Interview-only design grilling for Snapback architecture before any code.
  Use for IPC, storage/schema, capture threading, cross-platform contracts,
  ADRs, or when the user says grill me / design first / do not code yet.
---

# Snapback grill design

## Mode

**Do not write or edit product code** until the owner explicitly ends grilling (e.g. "design is done, implement").

Act as a skeptical interviewer. Goal: a bulletproof technical design, not a draft PR.

## Scope (when to grill)

Use this for: architecture, IPC contracts, SQLite migrations, capture/engine threading, cross-platform behavior, privacy/security boundaries, or anything tagged `decision` on the roadmap.

Skip for: typos, copy, one-line UI polish, obvious mechanical fixes with an existing pattern.

## Procedure

1. Open with one sentence restating the problem and the constraint surface (ADRs, decided-not-to-build, reflect-don't-block).
2. Ask **one** question at a time about edge cases, data ownership, failure modes, threading, migration/compat, or user-visible behavior.
3. Prefer concrete choices (A vs B) over open essays.
4. When enough is settled, produce a short **spec card**: goal, non-goals, files likely touched, invariants, test plan, open risks.
5. Stop. Tell the owner to start a **fresh/compact** implementation chat with only the spec card, preferably TDD.

## Hard constraints to probe

- Reflect focus drift; never block apps or auto-start sessions for the user.
- No remote subresources on shipped pages; release migrations append-only and idempotent.
- IPC four-file sync if commands change.
- Sole-author commits; no invented backlog.
