# Native SPLINE and SQUAD sampling

The native animation sampler now implements all five original interpolation
modes: LINEAR 176, SLERP 177, SPLINE 178, SQUAD 179 and STEP 180. The cubic
implementation follows the original preparation and evaluation arithmetic,
including separate incoming/outgoing controls, authored quaternion signs and
explicit float32 round points. The product contains C++ arithmetic and data
parsing; it does not execute the original program or include a CPU runtime.

This change is prepared as an isolated patch against the frozen first
`M3GAnimation` slice. The patch and manifest identify its exact base and output
bytes. It does not modify the frozen campaign app, add a complete battle
controller, or claim an Apple/GPU/device result.

## Public API and preparation lifetime

`M3GSequenceSampler` owns an immutable copy of one sequence's keys and prepares
its valid range once. Its `sample(float)` method reuses those controls. The
read-only `keyframes()` view exposes normalized values and both auxiliary
arrays for reference comparison. Keys outside the valid range are copied but
are neither normalized nor given controls.

`M3GAnimations` builds a sampler cache while decoding its sequences. Frame
sampling uses that cache. The existing `m3g_sample_sequence` convenience
function constructs a temporary prepared sampler, so callers evaluating a
curve repeatedly should retain `M3GSequenceSampler` or use `M3GAnimations`.
No original interpolation-mode behavior was replaced with a library curve.

The original time mapping is retained: CONSTANT clamps at `duration-1`;
LOOP wraps; equal adjacent times and wrapped valid-index ranges remain
supported; exact keys use the direct-value path. A one-key range still
performs the original preparation policy, then samples its key directly.
The constructor rejects unsupported mode/component counts, nonfinite valid
keys, zero auxiliary denominators and nonfinite prepared controls. The last
two checks explicitly bound malformed or degenerate curves; they do not
claim to reproduce historical NaN/exception behavior.

## Evidence and checks

Original addresses, formulas, quaternion helper order, source identity and
host boundaries are documented in `M3G_SPLINE_SQUAD_RECOVERY.md`. The synthetic
reference executes unchanged original preparation/sampling routines and the
original ARMv6 arithmetic library. Its record is
`m3g_validation/original_spline_squad.json`.

| Check | Observed result |
| --- | --- |
| Synthetic original cubic sequences | 19 |
| Original sample calls | 290 |
| Prepared values/tangents/controls | 546 scalar comparisons, all binary32 identical locally |
| Weighted sample outputs | 987 scalar comparisons, all binary32 identical locally |
| New native boundary/lifetime contracts | 10 passed |
| Existing native animation contracts | 43 passed |
| Existing Bumblebee samples | 7,325 scalar bits unchanged |
| Existing two-track weighted samples | 84 scalar bits unchanged |
| Existing controller mapping | 12 cases unchanged |
| Existing posed hierarchy | 5,088 scalars, same maximum error `1.1712109628803091e-6` |
| Actual corpus curve sampling | 27 files, 738 sequences, 70,786 times, 266,951 finite scalars |
| Actual model pose probes | All 24 model files passed six sampled poses |

The actual corpus pass checks finite decoding/sampling and previously
implemented rigid transforms. It is not a second original-instruction oracle
for every real asset. Its assets are hash-verified locally and are not added
to the source repository.

The original reference substitutes host float32 `sqrtf`, `sinf` and `cosf` at
explicit external call boundaries. This is not proof of bit identity with
historical iOS libSystem. Local native results match every recorded bit. The
portable C++ test requires exact bits for SPLINE and pure normalization; only
SQUAD controls/samples downstream of those host transcendental calls allow
an absolute error of `2e-6`. It prints the exact-bit counts and maximum error,
so an accepted cross-platform difference is not reported as exact equality.

## Tests and reproduction

Add `tests/m3g_spline_test.cpp` to the native host tests and register
`m3g_spline_sampling_contract` with no arguments. It uses the synthetic
`tests/m3g_spline_fixture.hpp`; no proprietary asset, Python package or CPU
runtime is needed. The existing animation test changes only its obsolete
"SPLINE unsupported" check to reject invalid interpolation mode 181.

Regenerate the fixture with:

```sh
python3 analysis_tools/m3g/spline_fixture.py \
  --reference native/research/m3g_validation/original_spline_squad.json \
  --output native/tests/m3g_spline_fixture.hpp
```

`tools/m3g_curve_probe.cpp` is an optional local-data inspection target.
`analysis_tools/m3g/spline_corpus_validate.py` combines it with the existing
pose probe using the original manifest and frozen corpus selection. The
original reference runner/dependencies remain development-only and are
listed in `analysis_tools/m3g/SPLINE_SQUAD_REFERENCE.md`.

## Remaining battle-rendering boundaries

The three actual battle scenes now sample every curve. Full pose/render
probes still stop explicitly at required operations:

| Resource | Remaining native probe boundary |
| --- | --- |
| 486 | Required Sprite3D nodes |
| 534 | Active camera FOV/node alpha/visibility properties beyond rigid transforms |
| 1212 | Required Sprite3D nodes |

Camera projection decoding and timeline helper arithmetic have separate
validated slices. Choosing and applying their live battle state, supporting
these properties, Sprite3D geometry and shared transparent ordering remain
integration work. No completion acknowledgement or fallback rendering is
added by this change.
