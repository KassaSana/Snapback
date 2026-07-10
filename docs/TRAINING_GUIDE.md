# Training guide — how much data, when to retrain

A short, practical guide for the training/deploy step. It answers three
questions: *do I have enough labels to train?*, *when should I retrain?*, and
*when is synthetic data enough vs. real labeled data?*

Every threshold below is enforced in code, not aspirational. The source of
truth is [`ml/training_pipeline.py`](../ml/training_pipeline.py)
(`validate_training_dataset`, `MIN_TRAINING_SAMPLES`, `MIN_SAMPLES_PER_LABEL`)
and the CI floors in
[`tools/benchmark_classifier_quality.py`](../tools/benchmark_classifier_quality.py).

---

## TL;DR

| Question | Answer |
|----------|--------|
| Minimum labeled samples to train | **8** (or `n_splits + 1`, whichever is larger; default `n_splits` is 5) |
| Minimum distinct focus states | **2** (a single-class dataset can't train — it only yields a majority stub) |
| Minimum samples per state | **2** |
| Practical target for a model you'd trust | **~150–250 labels across all 4 states** |
| When to retrain | After a meaningful batch of new labels, a workflow change, or when live predictions feel wrong |
| Synthetic data | Good for wiring/CI/latency; **not** for judging real focus accuracy |

---

## The four focus states

Labels map to four classes (see `FocusLabel` in
[`ml/labeling.py`](../ml/labeling.py)):

| State | Label value | Meaning |
|-------|-------------|---------|
| `DISTRACTED` | -1 | Off-task |
| `PSEUDO_PRODUCTIVE` | 0 | Looks busy, low real progress |
| `PRODUCTIVE` | 1 | On-task |
| `DEEP_FOCUS` | 2 | Flow / high focus |

The model is only as good as the balance across these. If you only ever label
`DEEP_FOCUS`, the trainer has nothing to contrast it with.

---

## Do I have enough to train?

Training is **blocked with a clear error** unless all of these hold
(`validate_training_dataset`):

1. **At least `max(8, n_splits + 1)` labeled samples.** With the default 5-fold
   time-series CV that's **8**. Below this, cross-validation folds would be too
   small to mean anything.
2. **At least 2 distinct label classes.** One class can only produce a
   majority-vote stub, which is explicitly refused from ONNX export
   (`is_majority_stub` in [`ml/export_onnx.py`](../ml/export_onnx.py)).
3. **At least 2 samples in the smallest class.**

If any fail, `pipeline_cli` exits non-zero with `Training blocked: …` and the
app surfaces a friendly message — you have not trained a model.

> **Meeting the minimum is not the same as having a good model.** 8 samples lets
> the pipeline *run*; it will not generalize. Treat the minimums as a floor for
> "the machinery works," not "the predictions are trustworthy."

### What "trustworthy" looks like

CI enforces these cross-validated floors on the synthetic benchmark
(`benchmark_classifier_quality.py`); use them as a sniff test for your own runs:

| Metric | Floor | Read it as |
|--------|-------|-----------|
| CV accuracy | ≥ 0.55 | Better than a coin-flip across 4 classes |
| Precision@10% distracted | ≥ 0.45 | When it's most sure you're distracted, it's usually right |
| Recall distracted | ≥ 0.30 | It actually catches distraction (the heuristic catches ~0%) |
| Recall lift over heuristic | ≥ 0.20 | The model earns its keep vs. the rule-based baseline |

Cite **CV metrics**, not in-sample accuracy. In-sample accuracy of ~100% on the
training join is expected and meaningless for generalization.

---

## When should I retrain?

Retrain when one of these is true:

- **You've added a meaningful batch of new labels** — roughly another 50+, or
  enough to change class balance. Retraining on one or two new labels won't move
  the model.
- **Your workflow changed** — new apps, a different job, a new project. The
  features (`is_ide`, `is_browser`, app/title signals) shift, so an old model
  drifts out of date.
- **Live predictions feel wrong** — the focus state shown in the app
  consistently disagrees with how you actually felt. Label a session honestly,
  then retrain.

You do **not** need to retrain on a schedule. This is a personal model; retrain
when your data or your work meaningfully changes, not by the calendar.

---

## Synthetic vs. real labeled data

The repo can generate synthetic training data
([`tools/generate_synthetic_training_data.py`](../tools/generate_synthetic_training_data.py)).
Know what it is and isn't for.

**Synthetic data is enough for:**

- Wiring up and smoke-testing the whole export → train → ONNX → reload path.
- CI quality gates and export-parity checks (Rust ONNX vs. Python match).
- Latency/soak benchmarks — inference cost doesn't care whether labels are real.

**Synthetic data is NOT enough for:**

- Judging whether the model reflects *your* focus. Synthetic rows are generated
  from assumed patterns, so high synthetic accuracy says the pipeline works, not
  that predictions will match your real behavior.
- Any claim like "the model is 90% accurate for me." Only real, honestly labeled
  sessions support that.

**Rule of thumb:** use synthetic data to prove the plumbing, then collect real
labels from your own sessions before trusting a deployed model. A few hundred
real labels spread across all four states beats thousands of synthetic ones for
a model you actually rely on.

### A note on the numbers you'll see

The app and CI report a *production-aligned* quality number from the Rust
`--classifier-eval` path, which runs the model through the same guardrails as
the live engine. The Python `onnxruntime` evaluators report *raw-model* numbers
(no guardrails) and are labeled `production_aligned=False`. When comparing
"how good is my model," prefer the production-aligned number — it's what you'll
actually experience. See
[`ml/classifier_quality.py`](../ml/classifier_quality.py) and
[`src-tauri/src/engine/classifier_eval.rs`](../src-tauri/src/engine/classifier_eval.rs).

---

## Commands

Train from your real exported sessions (default app DB location):

```bash
py -m ml.pipeline_cli --output-dir data --backend xgboost
```

Generate synthetic data and train (plumbing / benchmark only):

```bash
py -m tools.generate_synthetic_training_data --seed 7
py -m ml.pipeline_cli --db-path data/synthetic_focoflow.db --output-dir data --backend xgboost
```

Check model quality against the CI floors:

```bash
py -m tools.benchmark_classifier_quality --skip-train --enforce-gate
```

Training deps (needed for XGBoost + ONNX export):

```bash
pip install -r ml/requirements-train.txt
```

---

**Related:** [BACKLOG.md](BACKLOG.md) · [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md) · [SMOKE_TEST.md](SMOKE_TEST.md)
