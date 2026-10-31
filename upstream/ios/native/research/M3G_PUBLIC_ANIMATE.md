# Original public Group animation interface

The virtual slot used by the original battle timeline is the public
Object3D animation operation. This report closes that interface identity by
following the original constructors and call instructions. It does not claim
that the complete object graph or GPU was executed by the timeline fixture.

The inspected ARMv6 executable has SHA256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
Addresses below are virtual addresses in that image. The instruction ranges
are Thumb unless otherwise stated; inline switch data is interpreted as
data, not as the disassembler's possible instruction spelling.

## Interface construction

The timeline start helper at `0x3c260..0x3c268` obtains the public object from
its reference, loads that object's method table, and calls table offset
`0x24`. The update helper uses the same public interface.

| Original address | Evidence |
| --- | --- |
| `0x90540` | The named `CssGroup::Type() const` method returns engine type 9. |
| `0xb0bb8..0xb0bc2` | Public wrapper factory calls internal type slot `+8`, subtracts one, then dispatches through ARM switch helper `0xb9cb0`. |
| `0xb0bc6`, `0xb0bd8` | The table has 33 cases; type 9 selects index 8 and signed halfword `0x008b`, resolving to Thumb destination `0xb0cdd`. |
| `0xb0cdc..0xb0cf2` | Group case obtains/caches a `0xd0`-byte method table and calls constructor `0xac2d0`. |
| `0xac2d6` | Group constructor calls `0xae220`, then fills only offsets `0xb8..0xcc`. |
| `0xae226` | Node constructor calls `0xb27d2`, then fills only offsets `0x80..0xb4`. |
| `0xb27d8` | Transformable constructor calls `0xae674`, then fills only offsets `0x4c..0x7c`. |
| `0xae698..0xae69c` | Object3D constructor reads literal `0x12f`, adds architectural PC `0xae69e`, and writes Thumb function pointer `0xae7cd` to offset `0x24`. |
| `0xb0f40..0xb0f44` | Factory stores the constructed method table at public object offset zero and the internal serialization interface at public offset four. |

The derived initializers do not replace offset `0x24`. The arithmetic
`0xae69e + 0x12f = 0xae7cd` identifies the wrapper body at `0xae7cc`.

## Wrapper and sampler call chain

The wrapper at `0xae7cc` saves the public object, the signed time argument,
and the result pointer. At `0xae7ec..0xae7f4`, it loads the internal pointer
from public offset four and subtracts four to obtain the internal object's
primary base. At `0xae7f6..0xae7fc`, it calls:

~~~text
0x99e3c(internal_object, supplied_world_time, visitation_id = -1)
~~~

It writes the returned integer through the caller's result pointer at
`0xae800..0xae802`. The surrounding wrapper handles the engine's existing
exception/error machinery; the battle fixture does not exercise that path.

`0x99e3c` creates a visitation identifier when passed `-1`, avoids already
visited objects, iterates property-grouped animation tracks, checks their
controllers, accumulates active contributions, and dispatches the property
assignment. Its calls to `0x89034` reach the sequence sampler at
`0x8905e → 0x9346c`. It finally delegates referenced-object traversal through
the internal object's `+0x10` slot at `0x9a0d8..0x9a0e6`. These instructions
establish the animation-operation identity; they do not make the separate
record-only timeline fixture an end-to-end graph execution.

## Distinguishing the two interfaces

The internal `CssGroup` primary table is a different interface. Its offset
`0x24` points to the node render-enable operation at `0x9884c`. Looking up
that internal slot as if it were the public wrapper would therefore give
the wrong meaning for the battle call. The public table is constructed
through the factory chain above.

Some internal table entries are zero in the raw Mach-O data and are filled
by external relocations, including weak local-symbol bindings. Raw zero
words alone are not evidence of a missing engine method. The public slot
proved here is installed by explicit PC-relative constructor instructions
and does not depend on interpreting those unresolved raw words.

## Reproduction and limits

The local read-only trace was generated with the retained Capstone range
disassembler over `0x90540`, `0xb0b74..0xb0f5c`, `0xb9cb0`,
`0xac2d0..0xac30a`, `0xae220`, `0xb27d2..0xb282c`,
`0xae674..0xae812`, `0x99e3c..0x9a0f2`, and `0x89034..0x8909a`.
The private disassembly lives under `research/native_m3g/public_*`.

The repository-eligible battle reference remains
`m3g_validation/original_battle_timeline.json`: it executes the original
timeline arithmetic and original libgcc, then records time calls at this
public boundary. It does not execute the resolved wrapper, render a pose,
or validate a battle camera. Native sequence sampling has its own separate
original-function comparison in `M3G_ANIMATION.md`.
