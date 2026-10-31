# Native capture actions and ownership-derived counters

This component reconstructs the original capture state changes as portable C++.
It opens no executable, uses no CPU interpreter and contains no guest pointers.
Its development-only reference runner executes a separately supplied original
binary; that runner and its external Unicorn dependency are excluded from the
native product.

## Implemented boundary

`CaptureNetwork.sites` uses the existing `MissionSiteState` representation. The
site owner and the nested item owners remain separate. A successful item capture
at the first objective changes `sites[0].item_states[0][0]` to owner zero; it
does not change the site owner, advance the mission cursor, or rely on merely
moving to the tile. `MissionExecution` may observe this real state afterwards.

The original `0x548e2` writes an item owner at map+`0x3f8`; `0x5488c` writes the
site owner at map+`0x3e8`. Both call `0x4a852(map, false, true)`. The native
`refresh_capture_network` implements both independent arguments to that routine:
counter refresh and link-byte refresh. Capturing refreshes links and preserves
the current counter pair.

`perform_capture` performs the active/unused-action command gate, coordinate and
ownership availability checks, and the actual form check. Its caller must own
current-player selection and the stationary command phase. An inactive actor or
nonzero unit flag `0x2c` is rejected. A different owner at a matching site or item
coordinate makes capture available. These availability predicates do not check
the form. The selected form's signed byte 10 must equal one when the command is
invoked. Otherwise the original shows localized message ID 1145: the original
asset explains that capture requires robot form. Rejected native actions leave
ownership, links, counters and actor state unchanged.

The original human scan at `0x59fc4` visits sites, groups and items in stored
order. The first matching item wins. If no item matches, the last matching site
is the fallback. Selection does not independently filter ownership; the prior
availability check is separate. Coordinate bytes are read signed in the
original while unit coordinates are unsigned, so a coordinate byte >=128 does
not match a nonnegative unit cell. The native scan preserves these details.

After capture, original `0x430f4` sets unit flag `0x2c` to one. The native actor
receives the same action-used flag. Capture does not change spent movement.

## Link and counter calculation

`refresh_capture_network(network, factions, true, true)` reproduces complete
original routine `0x4a852`. Counter refresh first sets both counters to one.
Each owned site whose mapped faction is not -1 adds two to its owner's counter.
For each ordered group, the site faction and owner initially form the reference.
If the site is neutral, the first item with a non-neutral mapped faction becomes
the reference. Neutral items are skipped. While the group's observed factions
remain uncontested, each eligible item adds two to the reference owner's counter
and receives link byte one. Once a different faction occurs, that conflict
persists through the rest of the group: eligible items instead add one to their
own owner's counter and receive link byte zero. A new group starts a fresh
reference/conflict state. Distinct owners mapped to the same faction are not a
conflict; the reference owner receives the two-point contributions.

When link refresh is enabled, each processed item's old link byte is cleared
before its eligibility is considered. Disabling either refresh flag preserves
that entire family of output fields, including non-Boolean preexisting link
bytes. Counts are checked against the original byte-loop limits; owners must be
-1, 0 or 1. Invalid shapes and owners fail before any write.

The parent turn-recovery work traced `0x4e80c -> 0x4aa1e -> 0x4a852(true,true)`
followed by adding the current player's counter at map+`0x68` to its balance at
map+`0x58`. This module calculates that counter; it does not award currency or
execute a turn. The public field retains the original offset name to keep this
boundary clear. `initial_capture_network` supplies neutral item states, zero
links and the helper's one/one refresh baseline. This is **not original cold
mission state**: `0x57b80..0x57b9a` zeroes both counter and fund pairs. The first
script has no opcode 19 initial-funds command. A cold native session must set
`counters_68 = {0,0}` and initialize its funds to zero, then perform the genuine
refresh/payment sequence only at turn start. Parent `GameSession` does this
explicitly.

## Presentation requests and remaining work

The native result exposes these original requests; it does not pretend they
have been rendered or played:

| Original boundary | Native request |
|---|---|
| Human command menu close before capture | `close_command_menu` |
| `0x2ad12(controller,5,12)` when the owner's controller flag is zero | `audio_5_12` |
| `0x6eb98(site,group,item,owner)` | Item target plus the actor's new owner |
| `0x6d884(site,mapped_faction)` | Site target and `site_visual_faction` |
| Item branch `0x4e38c` | `clear_selection_flags` |
| Wrong form | `message_id = 1145` |

The item branch's `0x4e38c` clears the original selection/range flags at
`0xc4..0xc7` and associated display state. The site branch does not call that
same helper here. Full menu layout, sound playback, pylon/site visual updates,
turn scheduling, AI selection, undo after capture and save serialization remain
outside this module. A completed ownership mutation is not a claim that those
surrounding systems are complete.

## Validation

The strict host build (`g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror`)
passes 20 focused contracts. They cover the genuine first objective item,
wrong-form rejection, used actions, already-owned targets, separate site state,
conditional presentation requests, preserved counters, group conflict ordering,
shared faction mapping, neutral mapping, duplicate-coordinate ordering, signed
coordinates and atomic validation failure.

`capture_action_validation/original_capture.json` records 304 exact native/original
comparisons against original binary SHA-256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`:

| Compared path | Cases | Scope |
|---|---:|---|
| `0x4a852` | 192 | Entire original function, return and stack checked |
| `0x59fc4` selection region | 64 | Seeded block state, stops at original target/no-target decisions |
| `0x548e2` / `0x5488c` and `0x430f4` | 48 | Ownership/link prefix stops before presentation; action flag helper returns normally |

The selection boundaries are `0x5a0a8`, `0x5a14c` and `0x5a4b2`. Capture prefixes
stop before `0x54918` or `0x548ac`. No original audio, menu or renderer function
is substituted or executed. There are zero instruction patches and zero function
stubs. Original text is mapped read-only and checked unchanged for every case.
Synthetic inputs deliberately include all four refresh-flag combinations,
neutral and shared-faction mappings, stale link bytes, empty groups, competing
owners, duplicate coordinates and real ownership changes. The coordinate/
ownership availability predicates and presentation request mapping also have
direct instruction evidence; the full C++ allocation/exception-based original
availability wrappers are not claimed as executed by this runner.

## Build integration

Add `src/CaptureAction.cpp` to the native core. Add
`tests/capture_action_test.cpp` as `g1_capture_action_tests`, linked to the core,
and register `capture_action_contract` with no fixture arguments. Parent owns
CMake; this work did not edit it. Reproduction commands are in the analysis-tool
README. Source and report hashes are recorded in `CAPTURE_ACTION.json`.
