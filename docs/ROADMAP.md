# Roadmap

This is the live backlog.

## Start here

1. Define the v1 support matrix and release criteria.
2. Verify the macOS capture and desktop UI on a fresh machine.
3. Finish native tray and overlay behavior on macOS and Linux.
4. Add ordered SQLite schema migrations and a compatibility fixture corpus.
5. Remove N+1 reads from analytics, summaries, and session history.

## Product

- [ ] Finalize v1 focus-mode semantics and score thresholds.
- [ ] Add launch-on-login support for macOS and Linux.
- [ ] Add richer training-data review before deployment.
- [ ] Add an in-app support bundle for diagnostics.

## Platform

- [ ] Verify accessibility and event-tap behavior on real macOS hardware.
- [ ] Implement native Linux tray and overlay.
- [ ] Add interactive desktop smoke coverage where the runner permits it.
- [ ] Document packaging and signing for each release target.

## Reliability

- [ ] Add explicit schema versions and forward migrations.
- [ ] Expand storage fixtures for fresh, aged, large, and corrupt databases.
- [ ] Fuzz title parsing, JSON command input, and event decoding.
- [ ] Add a bounded queue metric dashboard to the diagnostics card.

## Performance

- [ ] Move long analytics reads outside the storage write lock.
- [ ] Profile engine tick latency under sustained input.
- [ ] Keep the exact feature-vector golden test current whenever model inputs change.
