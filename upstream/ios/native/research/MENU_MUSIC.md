# Native menu music maintenance

This follow-up recovers the separate original menu maintenance body at
`0x36eae..0x36f22`. It builds on the frozen map/battle maintenance delivery;
it does not change that selector or the Apple audio adapter. The original
ARM executable is an offline reference only.

## Exact request order

1. Read music enabled at `app+0xa4`; if zero, emit no request.
2. Read application mode at `app+0x1c0`; modes 3 and 100 emit no request.
3. Compare current menu `app+0x244` with `app+0x258`; equality emits no
   request. This is an identity comparison, including two null pointers.
4. Call operation 6 through original wrapper `0x2ad6c`. This queries any
   playing voice. If true, emit no further request.
5. Walk the current menu and its parents at `menu+8`. If any visited menu
   equals `app+0x260`, `app+0x278`, or `app+0x268`, emit no play request.
6. If the acyclic chain reaches null without an excluded menu, request
   operation 5, slot 0.

The prompt comparison occurs before the playing query. The three ancestry
comparisons occur after that query. An enable-sounds prompt in the parent
chain is not itself an ancestry exclusion. A null ancestor-exclusion pointer
never matches a traversed menu: traversal stops before visiting null.

`MenuMusic.hpp` separates the pre-query guard from the post-query idle
selection so the host preserves this order. It uses native object identities
and parent links. It neither reads original addresses at runtime nor needs an
interpreter, original executable, compatibility runtime, or an audio duration.

## Concrete original contexts

Resource identities below come from original constructor `0x31a22` and the
English text table extracted from the pinned `iphone.big`. Constructor
`0x5d9b4` clears the parent at `menu+8` (`0x5d9f4`) and stores its fourth
argument at `menu+0x28` (`0x5da22`). Opener `0x30d5a..0x30d7e` attaches the
previous current menu only when that flag is nonzero and no parent exists,
with explicit root exclusions. Thus the main menu's zero constructor flag
prevents inheriting a pause-menu ancestor when returning to Main Menu.

| Original app field | Constructor evidence | Native context | Menu maintenance behavior |
| --- | --- | --- | --- |
| `+0x24c` | Constructor call `0x31d16`, store `0x31d34`; title ID 1169, MAIN MENU; flag 0 | Main/profile panel; its Story, Showdown and Arena map selection sheets | Query any voice; if idle, slot 0 |
| `+0x258` | Title ID 1058, ENABLE SOUNDS? at `0x31ff0..0x32022`; constructor `0x32162`, store `0x3217a` | No equivalent startup prompt in current native UI; represented in portable policy | Exact current identity suppresses even the query |
| `+0x260` | Title ID 738, PAUSED at `0x31ed0..0x31f06`; constructor `0x31f6e`, store `0x31f8c` | Pause panel and descendants | Query only; no slot 0 |
| `+0x268` | Twelve unit-action entries at `0x324b8..0x327d8`; empty title; constructor `0x3284a`, store `0x32866` | Original unit action menu; current native actions are integrated into gameplay UI | This menu or an ancestor suppresses slot 0; portable policy preserves it |
| `+0x2a8` | AUTOBOTS ID 501, DECEPTICONS ID 1315, SELECT FACTION ID 1022 at `0x32d1e..0x32da6`; constructor `0x32dfe`, store `0x32e46` | Arena faction picker | Query any voice; if idle, slot 0 |
| `+0x278` | AI ID 580, HUMAN ID 690, initial SELECT OPPONENT title ID 1313 at `0x32bf0..0x32c8c`; constructor `0x32ce8`, store `0x32d06` | Arena opponent picker and descendants | Query only; no slot 0 |

The unit-action entries identify that menu without relying on its empty title:
two CAPTURE entries (1059,755), UPGRADE (944), ACTIVATE UNIT (785), ACTIVATE
TAPE (1055), REPAIR (770), TRANSFORM (1103), ATTACK (939), TECH SPECS (1141),
UNDO (1077), GO TO NEXT UNIT (1297), END TURN (364).

Original Arena transition code at `0x35944..0x359c6` opens faction `+0x2a8`
after map-list `+0x2a4`, then opponent `+0x278` after faction. It also replaces
the opponent title with CYBERTRON ARENA (1190) at `0x3598c..0x3599a`; the
constructor's initial title alone must not be mistaken for the final visible
Arena heading. The native picker order matches these original transitions.

Original menu opening sets mode 10 at `0x316ee..0x316f4`. Loading/transitions
use mode 3; gameplay uses mode 100. Gameplay, results and other campaign
presentations keep their existing separate music owners.

## Host changes

The startup/profile UI appears before `_loaded` becomes true and before the
Metal display loop runs. Calling menu maintenance only from simulation would
therefore leave startup silent. The host adds a weak-controller, main-run-loop
timer at the already defined 30-ms simulation step, and also evaluates music
after a supported panel becomes visible. This is a native scheduling choice;
the reference comparison proves the request decision, not an original Apple
timer implementation or an exact delay after the end of a sound.

The timer skips inactive applications and unsupported panels. The existing
Apple adapter continues to own real voice completion, interruptions, output
availability, independent preferences and actual sound scheduling. The timer
does not advance gameplay, affect the 60-Hz display target, create an automatic
audio loop, or estimate whether a sound has finished.

The Arena opponent panel gets its own `NativePanel` value so it cannot be
treated as the eligible faction picker. The fixed native menu graph is
acyclic. Current unit action buttons remain in gameplay mode and therefore
do not enter the global menu music path.

Enabling Music while on Main Menu previously dispatched `_lastMapMusic`
unconditionally when a prior map existed. The follow-up limits that existing
explicit terrain restart to Pause. Main Menu now uses its own any-voice query
and slot-0 policy. It does not forcibly interrupt a still-playing voice when
opening a menu. No operation-4 behavior is inferred or introduced. The existing
Pause preference behavior is retained, not claimed as a reconstruction of the
original settings UI.

Native error panels and campaign ending/message presentations are not mapped
to guessed historical menus. This recovery does not claim a complete original
menu UI, original cold preference defaults, or original transition music for
every native extension.

## Original-function comparison

`analysis_tools/audio/menu_music_oracle.py` executes the original body and
wrapper unchanged. It seeds independent synthetic application/menu records
and substitutes only the audio-output boundary `0x2ad12`, recording its
operation/argument and supplying the independently chosen any-voice-playing
answer for operation 6. It does not supply a menu classification or a selected
track to the original body.

**10,608 original cases matched 17,392 scalar comparisons with zero
differences.** Cases cover enabled/disabled music, active/inactive voices,
application modes including 3,10,100 and signed extremes, exact-current
exclusion, all three ancestry exclusions, null identities, aliased exclusions,
prompt-as-parent, and acyclic chains through depth eight. Original text remains
read-only and unchanged. Application and menu storage remains unchanged in
every case. The native probe builds with C++17, `-O2 -Wall -Wextra -Wconversion
-Werror -pedantic` and also reads the generated fixture independently.

Reference hashes:

- Main ARMv6 executable: `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
- Original ARMv6 libgcc: `d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83`.
- Resource archive: `663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4`.

The machine-readable report is
`research/audio_event_validation/menu_music_validation.json`. The reference
comparison does not test UIKit timers, Objective-C++ compilation, Apple audio
device behavior, audibility, or iPhone playthroughs. Apple compilation and
physical testing remain separate gates.
