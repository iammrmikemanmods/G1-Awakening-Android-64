# Sprite animation checkpoint restoration

`SpriteAnimation::restore(steps, state)` restores a native checkpoint without
restarting playback. Supply the same decoded animation sequence used when the
state was saved. This is a new native state-management API; the original start
and advance transcriptions remain behavior-identical.

The complete state contains step index, elapsed milliseconds, signed repeat
count, reverse flag and sticky completion flag. Restoration validates the
sequence, rejects an out-of-range index and rejects negative elapsed time.
Elapsed time may exceed the newly selected step duration: original `0x72cb4`
changes the step without changing elapsed time. Completion likewise does not
require the saved index to be an endpoint. No random words are consumed.

## Validation

The existing independent `animation_oracle.py` ran unchanged original
`0x72d80` and `0x72cc0`; the new native probe restores its complete state before
every tick. All 1,616 cases / 17,776 state observations / 124,432 scalar
comparisons passed, including 16,160 restored ticks. The corpus uses all 170
original descriptor animations plus 32 synthetic sequences, both playback
directions, four repeat values and boundary/long deltas. The original machine
code remained read-only and unchanged. Existing allocation/free, byte-buffer
initialization and geometry-refresh recording boundaries remain explicit in
`original_animation_restore.json`.

The dependency-free native test now passes 42 checks, including rejected saved
states, preservation of elapsed time after step selection, completed-state
continuation, and overflow rejection without mutation. This establishes native
state continuation, not GPU rendering, device execution or a campaign save.

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -I native/include \
  native/src/SpriteAnimation.cpp native/tests/sprite_animation_test.cpp \
  -o /tmp/g1_sprite_animation_restore_test
/tmp/g1_sprite_animation_restore_test

g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -I native/include \
  native/src/SpriteAnimation.cpp analysis_tools/sprite/animation_restore_probe.cpp \
  -o /tmp/g1_animation_restore_probe
python3 analysis_tools/sprite/animation_oracle.py \
  --binary LOCAL_ORIGINAL_ARMV6 --unicorn-dir LOCAL_UNICORN_MODULES \
  --atlas-decoder native/tests/generate_atlas_reference.py \
  --audit-dir LOCAL_ASSET_AUDIT --native-probe /tmp/g1_animation_restore_probe \
  --output /tmp/g1_original_animation_restore.json
```

The existing CMake sprite animation test already covers these additions; no new
target or product dependency is needed. Original assets and executable bytes
are local oracle inputs only and are not included in this checkpoint.
