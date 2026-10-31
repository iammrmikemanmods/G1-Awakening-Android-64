# Original map initialization and terrain RNG order

This is static control-flow evidence from the pinned original ARMv6 image, not
an observed live initialization trace. It identifies a required ordering for
the native session and explicitly does not claim that the current bulk world
bootstrap has reproduced every intervening original update.

## Opcode9 completes renderer stages before the next command

The opcode9 handler at `0x56c20..0x56ed8` normalizes controller field`+0x78`
to3 when it is outside3..7. The actual switch table at`0x56c48` selects:

| Controller phase | Original target | Operation |
|---:|---|---|
|3|`0x56c64`|Load grid/lookup, apply style overrides and initialize turn state.|
|4|`0x56e50`|Conditional style music, then renderer stage0.|
|5|`0x56e68`|Renderer stage1.|
|6|`0x56e68`|Renderer stage2.|
|7|`0x56e90`|Renderer stage3, cursor/camera refresh, set map byte`+0x14` to1.|

At `0x56e80`, phases4..6 call `0x5d250(owner, phase-4)`. Phases3..6 decrement
the script cursor at `0x56e84..0x56e8c` and increment controller phase at
`0x56ecc..0x56ed8`; the dispatcher therefore revisits opcode9. Phase7 skips
that cursor decrement. A following opcode6 cannot execute until these map-load
stages have completed. The dispatcher phase/continuation path was independently
cross-checked by the mission-recovery owner.

For stages0..2, `0x5d250` calls renderer `0x6e254`. Stage1 branches at
`0x6e28c..0x6e292` to `0x6eb5c`, whose call at`0x6eb62` executes terrain
constructor`0x6a2a4`. Pool initialization at`0x6b606..0x6b660` calls
`0x64ebc` once per slot, which consumes the shared generator via`0x64e64`.
Those initial pool draws precede following script spawns.

Renderer stage2 branches to`0x6eb6c` and calls sprite-world setup`0x68efc`
at`0x6eb72`. This setup also contains direct shared-random calls:
`0x69b56`, `0x69b9c`, `0x69e3c`, `0x69e84`, `0x69fe4`, `0x6a054` and
`0x6a148`, all targeting`0x2b43c`. Their execution depends on the style and map
branches. They are additional draws, not part of the already validated style1
pool. The number must be derived by executing/reconstructing those branches;
no fixed skip count has been established here.

## Native integration consequence

Creating a terrain pool after executing initial script spawns changes the
shared RNG order. A faithful construction interface needs an ordered map-init
boundary: terrain stage1, sprite stage2, remaining map-load completion, then
subsequent script commands and their spawns. It must also preserve state when
restoring a checkpoint; restoring a pool must not recreate it or consume new
draws.

After map byte`+0x14` becomes1, the ordinary map-update path can call the
renderer/terrain update. Periodic pool reselection may therefore interleave
with later script commands according to actual simulation deltas and pause
gates. This note proves the initial construction order, not a complete timing
schedule, historical seed, complete startup draw count or full-game RNG parity.

The first supplied mission uses style0. Its zero-outside-padding first-pass
data gives98 candidate-capacity records and14 pool slots, but that metadata
count does not account for stage2 draws or periodic callbacks. The style0/2
sprite-pool implementation and original comparisons remain active work.

`terrain_validation/map_init_rng_order.json` records the original image hash,
switch targets, exact callsite instructions as addresses, and hashes of the
inspected source ranges. No original resource payload or machine-code image
is included in the native source tree.
