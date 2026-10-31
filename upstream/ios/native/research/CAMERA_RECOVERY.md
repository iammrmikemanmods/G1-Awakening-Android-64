# Tactical camera evidence: conditional setup, not a finished view

The original camera setup has been traced and exercised in an isolated
development oracle. **Neither supplied viewport is claimed to be the observed
logical viewport of the first playable frame.** The original app declares
landscape-right orientation, and the rendering path includes display rotation.
The native app must retain its inspection-camera label until the full viewport,
camera hierarchy and display transform are connected and verified.

No camera implementation, original code, or instruction interpreter has been
added to the native product by this analysis.

## What the original instructions establish

At `0x6e40a..0x6e444`, the camera's projection call receives these four values:

1. A 30-degree perspective parameter.
2. Logical width divided by logical height, with original float arithmetic.
3. Near distance 1.
4. Far distance 8.

The next orientation call (`0x6e446..0x6e45e`) receives
`[-45 degrees, axis X=1, axis Y=0, axis Z=0]`.

At `0x6e4e8..0x6e52a`, the camera height/depth globals `0xd8e68` and `0xd8e6c`
are both initialized to 320, then the transform interface receives translation
`(0, 3.2, 3.2)`. This is an initialization step, before the mission camera target
is clamped and applied.

The first mission stream's opcode 4 has arguments `[1,2,1]`. The dispatcher
calls `0x557f4`; its immediate path reaches `0x5cb84`, `0x5cb42`, `0x5cb26`,
then `0x66c20`. The focus operation uses integer world coordinates from the
target tile and optional offsets. With those offsets zero:

```text
focusX = clamp(tileX * 32, minimumColumnWorld,
               mapWidth * 32 - minimumColumnWorld)
focusZ = clamp(tileY * 32, trunc(minimumRowWorld), trunc(maximumRowWorld))
cameraTarget = ((focusX + 16) / 100,
                heightWorld / 100,
                (depthWorld + 32 + focusZ) / 100)
```

Original `0x66c20` passes those three floats and an interpolation value to
`0x6688c`. The immediate path passes 1. This oracle records that boundary;
it does not execute the complete subsequent camera hierarchy or pan behavior.
The non-immediate branch also exists and must not be silently replaced by an
instant jump when the native mission interpreter is extended.

## Results for the two supplied viewport cases

The oracle executes the original bounds function `0x6585e`, with a 15×14 map
and the original initial height/depth of 320. It then focuses on the first
mission's tile target `(1,2)`.

| Supplied logical viewport | Aspect argument | X margin in raw units | Row bounds in raw units | Clamped focus | Camera target |
|---|---:|---:|---|---|---|
|320×480|0.6666666865|34|123.5881348 .. 329.8445740|(34,123)|(0.5,3.2,4.75)|
|480×320|1.5|85|89.4311905 .. 358.2676392|(85,89)|(1.01,3.2,4.41)|

Values are rounded for display; `terrain_validation/original_camera_setup.json`
records the exact float bits and additional target cases `(5,7)`, `(0,0)` and
`(14,13)`. Both rows are conditional input/output evidence. Selecting the second
row solely because the image is landscape would still leave the engine's
logical viewport and display-rotation relationship unproven.

The earlier desktop capture in probe38 is 480×320. A separate read-only PC GL
diagnostic log records a 320×480 GL viewport and a rotated orthographic
projection. That log contains no tactical 3D projection matrix, and its runtime
provenance was not re-established by this camera task. It therefore supports
caution about orientation, not a claim that either camera row is the complete
original playable view.

## Exact oracle scope and substitutions

`analysis_tools/terrain/camera_oracle.py` executes the unchanged original camera
client instructions and maps the unchanged original ARMv6 libgcc for arithmetic.
It supplies a synthetic M3G camera/transform interface whose setters record
their actual arguments. Allocation, interface ownership and the logical screen
context are explicit infrastructure boundaries. The target function `0x6688c`
is a recording endpoint.

Four original libSystem trigonometric imports use the host's `libm` functions:
`tanf`, `atanf`, `tan` and `cos`. The report records each input and output.
Historical iOS libm bit identity is **not claimed**. This is narrower evidence
than a complete original-device camera execution. Camera bounds are derived by
the original instructions under those stated math boundaries; no arbitrary
return values are substituted to make the result pass.

The original executable, libgcc, analysis source and shared oracle source hashes
are pinned in the report. No original instruction bytes are patched.

Reproduce from the project directory containing `analysis_tools/`:

```sh
python3 analysis_tools/terrain/camera_oracle.py \
  --binary LOCAL_ORIGINAL_EXECUTABLE \
  --libgcc LOCAL_ORIGINAL_ARMV6_LIBGCC \
  --unicorn-dir LOCAL_DEVELOPMENT_UNICORN_DIRECTORY \
  --output LOCAL_VALIDATION/original_camera_setup.json
```

The remaining gates are live logical-viewport binding, full scene and display
transforms, native projection/depth-convention integration, animated camera
movement, visual comparison and iPhone execution. The recovered unit model
transforms are a separate component; they should be combined with this camera
only after those coordinate relationships are established.
