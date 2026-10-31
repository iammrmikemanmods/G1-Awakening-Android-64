# Original first-use support tutorial

The support tutorial request is recovered from the unchanged original
`G1awakening` executable with SHA-256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
The new oracle executes the branch that the build 006 support-phase oracle
explicitly stopped before. No executable instruction is patched.

## Exact recovered behavior

At the end of the nonempty support-phase entry, `0x58796..0x5884e`:

1. Scans all **six allocated helper slots**, skipping null pointers, for a
   helper whose owner byte equals the current player byte at map `+0xa8`.
   This branch does not read the helper-count field. Its six-slot scan is
   separate from the preceding presentation's maximum of five visible cues.
2. Reads the **currently selected profile's** configuration byte `+0x0e`.
   The unchanged accessor `0x2b5e6` selects it using app `+0x3b4` and the
   pointer table at app `+0x3a8`.
3. Requires that flag to be nonzero, an owned helper to have been found,
   and the current player's **signed controller byte to equal zero**.
   Negative values and other nonzero values do not qualify.
4. Clears the selected profile's flag **before constructing the message**,
   then requests localized resource `0x21ff04a8` (text ID 1192), using the
   message presenter with argument 1. It destroys the temporary text after
   the call. The branch itself performs no filesystem save.

The implementation is the header-only `g1::support_tutorial_entry` in
`native/include/g1/SupportTutorial.hpp`. It returns the resulting pending
flag and whether to request that original message. It never changes units,
combat results, random state, support timing, or mission state. The caller
must process one decision per battle entry and publish the returned flag
before requesting the message.

## Profile lifetime and persistence evidence

The default/reset prefix `0x440f2..0x4410a` writes `1` to byte `+0x0e`.
The constructor calls this reset routine at `0x44360`. A separately reviewed
selected-profile reset path calls it at `0x30560` and subsequently invokes
the selected-profile save function `0x2fd74` at `0x3058e`.

The original profile serializer prefix `0x443ca..0x4441e` writes these
fields through its scalar output helpers:

| Stream byte offset | Configuration field | Encoding |
| --- | --- | --- |
| 0 | `+0x00` | byte |
| 1 | `+0x04` | 32-bit word |
| 5 | `+0x08` | 32-bit word |
| 9 | `+0x0c` | byte |
| 10 | **`+0x0e` support tutorial pending** | **byte** |
| 11 | `+0x0f` | byte |
| 12 | `+0x0d` | byte |

The reader prefix `0x4448a..0x4451a` restores the same field sequence and
normalizes each boolean byte, including `+0x0e`, to zero or one. All 256
possible support-flag byte values were passed through the unchanged writer
and reader prefixes. The writer preserves the input byte; the reader maps
zero to zero and any nonzero value to one.

These tests record the scalar stream endpoints; they do not execute original
disk IO, the rest of the profile records, or the full constructor/reset caller.
They establish that this is persistent selected-profile state, not a flag
to reset on every mission or every app launch. A native atomic write before
showing the message is a host durability policy; it must not be described as
an immediate disk write made by the original tutorial branch.

## Bounded verification

`original_tutorial.json` records the pinned input and oracle hashes,
execution ranges, recording boundaries and profile-prefix results.
`native/tests/support_tutorial_original_vectors.txt` contains 2,202 captured
input/output rows, with 6,606 output scalar comparisons. They cover:

- Empty and sparse helper slots; each of the six positions as the only
  eligible helper; the sixth slot remains eligible even though only five
  helpers are displayed by the support phase.
- Both normal players, all 64 ownership masks and prepared unusual owner
  bytes to distinguish equality from truthiness.
- All 256 pending-flag bytes and all 256 signed controller-byte values.
- Already-consumed flags, enemy-only support and computer turns.
- Selected-profile identity, state mutation limited to its pending flag,
  and the original clear/construct/request/destruct event order.

The native C++ test also verifies unchanged helper identities and no second
request after consuming the returned flag. A warning-enabled C++17 build
passes all captured cases. Exact results and file hashes are recorded in
`portable_validation.json` and `MANIFEST.json`.

The oracle uses Unicorn only as an offline development dependency. Neither
it, original executable bytes, nor test fixtures are linked into the iPhone
app. The original message text is obtained at runtime from the supplied game
archive; it is not replaced with new prose.

## Presentation boundary

`0x301aa` text construction, `0x46df2` message presentation and `0x604b4`
text disposal are recording boundaries. The original message presenter is
not executed by this oracle. Its separately reviewed leading instructions
check presenter byte `+0x34` for argument 1 and reject presenter phase 6;
the broader UI-state meaning of those guards is not established here.

The current native host already owns message display, drawn-token input
gating, support-clock suspension, pause/resume and session replacement.
Integration must preserve those owners. Request predicate parity is distinct
from original popup artwork, original presenter-state parity, physical XR
rendering, or a full rendered campaign playthrough. None of those device or
visual claims is made by these fixtures.

## Reproduction

Set `G1_ORIGINAL_BINARY` to the extracted, hash-matching original executable
and `G1_ORACLE_PYTHON` to a development Python package directory containing
Unicorn 2.1.4. From the repository root:

```sh
python3 native/research/support_tutorial_recovery/oracle.py \
  --binary "$G1_ORIGINAL_BINARY" \
  --unicorn-dir "$G1_ORACLE_PYTHON" \
  --output native/research/support_tutorial_recovery/original_tutorial.json \
  --fixture native/tests/support_tutorial_original_vectors.txt

c++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -I native/include \
  native/tests/support_tutorial_test.cpp -o /tmp/g1-support-tutorial-test
/tmp/g1-support-tutorial-test native/tests/support_tutorial_original_vectors.txt
```
