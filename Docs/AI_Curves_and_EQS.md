# AI curves and EQS authoring

These changes apply to the plasma.ai package's AI Behavior and AI EQS Query assets.

## Response curves

New response curves evaluate normalized input directly at the authored X coordinate. A point
at X = 0.25 is sampled at input 0.25. The first value is held flat before the first point and
the last value after the last point. Extra control points are not inserted, preserving the
authored interior shape and tangents.

- An empty curve is identity: output equals normalized input.
- A one-point curve is constant.
- Output is clamped to 0–1.
- Curve thumbnails and scoring previews share runtime sampling semantics.

### Existing assets

Version-1 consideration and EQS test objects are upgraded with `LegacyCurveDomain` enabled.
Version-1 runtime resources also retain the old domain. With this setting, input 0–1 maps
onto the first-to-last control-point interval, as before. New objects default to authored-X
behavior. Saving produces version-2 runtime resources.

To migrate, select the consideration/test object and disable `LegacyCurveDomain` in the
Properties panel. This is an ordinary undoable property edit. No control points are rewritten.
EQS cards identify legacy curves, and curve tooltips explain the mode. Compare the preview
before and after changing the setting.

Curves with endpoints already at 0 and 1 behave identically in either mode. Partial-range
curves change intentionally when migrated; retune them before deploying. Updated resources
require the updated AI plugin and should be distributed together.

## EQS test controls

The test card exposes filter conditions, limits, **Invert filter**, **Invert score**, and
**Missing data**. Filter controls are hidden for Score Only; score inversion is hidden for
Filter Only. Edits use document undo/redo transactions.

Filtering and scoring are independent:

1. The test measures a candidate and produces its normalized raw score.
2. Missing-data handling applies if the measurement could not be evaluated.
3. A filtering test evaluates its condition, then optionally negates that decision.
4. A scoring test applies its curve and optionally uses `1 - curve(raw)`.
5. Surviving, non-skipped scoring tests contribute `weight * score` and their weight.

The existing weight-normalized sum and result-selection rules remain. A final score of zero
is still omitted from runtime results; passing filters alone does not guarantee a returned
result when scoring tests are present.

### Filter conditions

| Condition | Meaning |
|---|---|
| Legacy score > 0 | Original raw-score filter; default for compatibility |
| True | Raw test result is at least 0.5; useful for boolean tests |
| Value >= Min | Native measurement is at least the limit |
| Value <= Max | Native measurement is at most the limit |
| Value between Min / Max | Inclusive native measurement range |
| Reachable | Full path found; use with Path Length |
| Direct path | Navmesh raycast reaches the candidate; use with Reachable Approx |

Numeric measurements are distance/path length in meters, direction angle in degrees,
cover-facing dot product, cover quality (Low = 0.5, High = 1), or the raw result for tests
without a separate measurement. Distance uses XY distance. Multi-position Distance contexts
combine measurements with Min/Max/Average independently of combining scores. Min measurement
means nearest context; it does not mean every context lies in a range.

Path Length records the maximum finite float when no complete path is found. Use Reachable
as a separate filter when reachability must remain mandatory alongside an inverted distance
condition. Approximate reachability cannot prove that a detour exists.

**Invert filter** negates the selected condition; it does not complement the raw score.
Inverting Between 5 and 15 rejects both boundaries and accepts values outside.
**Invert score** changes preference without changing eligibility.

LOS `PreferVisible` still defines the base result. With it off, blocked sight scores 1;
with it on, visible sight scores 1. Inversion applies afterwards. Avoid reversing the same
intention twice. Min combines the worst result across contexts, Max the best, and Average
the mean; inversion occurs after this combination.

### Examples

- Require outside 5–15 m: Distance, Filter Only, Between, Min 5, Max 15, Invert filter on.
- Require hidden from Threat: LOS, Filter Only, PreferVisible off, True.
- Require visible using that same LOS base setup: enable Invert filter.
- Prefer the opposite of an authored scoring curve: enable Invert score.
- Require an actual path regardless of scoring band: Path Length, Reachable.
- Require a direct walk: Reachable Approx, Direct path. A detour's legacy score of 0.25
  passes Legacy score > 0 but fails Direct path.

## Missing data

New tests default to **Reject candidate**. Version-1 assets retain **Legacy behavior**.

| Policy | Outcome |
|---|---|
| Reject candidate | Candidate is rejected even for Score Only |
| Skip test | No rejection and no scoring weight contribution for this candidate |
| Fail query | Query terminates with MissingData status |
| Legacy behavior | Retains the previous unavailable-data fallback |

Missing contexts, unavailable physics/navigation, incompatible payloads, stale handles,
and invalid measurements are recorded separately. Inversion never applies to unavailable
data. Invalid measurements cannot introduce non-finite scores through legacy handling.

## Validation and debugging

The editor checks context references and case, duplicate/reserved slot names, missing
providers, radius/scoring/filter ranges, weight validity, payload compatibility, and navigation
condition compatibility. Test-card messages identify the affected test. Root configuration
errors stop preview and transformation; test-specific warnings are logged on transformation.
Some warnings can be intentional when using a missing-data policy.

Local preview uses shared runtime filter/curve/inversion processing. Its generated world,
single marker for authored contexts, LOS occluder, and navigation remain simulations. Custom
tests without simulation support report unavailable preview measurements. Validate against
real geometry with an EQS Query Test component.

Click a candidate to inspect pass/reject/skip/not-run outcomes. Hover a breakdown row to see
measurement, raw score, curve/inversion output, weighted contribution, and data status.
Stage counts show how many candidates remain after each executed test.

In simulation:

```text
AI.EQS.VisualizeQueries 1
AI.EQS.VisualizeScores 1
```

The overlay retains rejected candidates with their rejecting test and reason, displays
per-stage counts, query age, and resolved context markers, and expands the winner's score
breakdown. Full per-test traces cover the first 12 tests; a later rejection still retains
its test index and current measurement. Recent query debug history remains bounded.

## Regression checks

The package's `AiSemanticsTests` executable target covers curve boundaries, legacy
compatibility, utility/EQS serialization, numeric/boolean filters, inversion, missing-data
policies, measurements, and trace recording. See the package's `Tests/README.md` for commands.
