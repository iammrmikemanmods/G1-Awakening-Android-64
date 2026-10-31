# Native M3G name and Node bindings

`M3GBindings.hpp/.cpp` implements the recovered name hash, bounded Node
lookup, explicit model-root selection, form-node lookup and model-camera
chain. The product code is portable C++17. It contains no guest execution,
original executable, asset payload or battle-phase controller.

The native implementation matches **34 original hash calls and six original
Group-tree searches**. Both optimized and UBSan builds additionally pass
**43 focused contracts**. An independent serialized-data audit passes all
**27 pinned resources**, including 513 Node searches and 2,677 field/structure
comparisons with zero differences. These results establish binding behavior;
they do not establish animation playback, rendering or device execution.

## Public interface

| API | Behavior |
| --- | --- |
| `m3g_name_hash(name)` | Hashes the supplied bytes in reverse, with original signed-byte and uint32 wrapping semantics |
| `m3g_transform_name_hash(base)` | Hashes `base + "Transform"`; always appends the suffix |
| `M3GNodeBindings(scene)` | Copies and validates minimal object/Node hierarchy metadata |
| `find_node(root, user_id)` | Returns the first self-first, child-order depth-first Node match, or `nullopt` |
| `select_loaded_container_group(children)` | Selects compatible child0, otherwise requires compatible child1; later children are never searched |
| `form_nodes(root)` | Returns independent optional `robotTransform` and `carTransform` Node IDs |
| `model_camera(root, kind)` | Looks up `roboCAMTransform` or `vehiCAMTransform`, then follows exactly two child0 accesses to a Camera |

The constructor also accepts a `vector<M3GObjectInfo>` for focused tests. IDs
remain distinct and need not be contiguous or numerically ordered. Child lists
define the hierarchy; the copied metadata does not retain borrowed pointers.

## Recovered behavior and explicit boundaries

### Name hashing

Original `0x26b4c` starts at zero and processes signed bytes in reverse order.
Each step wraps `state * 0x19660d + 0x3c6ef35f + signed_byte` to 32 bits. The
final two's-complement absolute-value bit pattern is masked with `0x3fffffff`.
The native code uses unsigned arithmetic throughout the wrap and absolute
value, avoiding C++ signed overflow, including the most-negative bit pattern.

Original `0x2701c` appends the literal `Transform` before hashing. The native
helper obtains the same result without allocating the concatenated name:
it reverse-hashes the suffix first, followed by the base name. Empty input and
an already suffixed input retain their original distinct behavior. Bytes are
case-sensitive and are not recoded or Unicode-normalized. Embedded NUL in a
typed `string_view` is explicitly rejected rather than interpreting a hidden
prefix as a different name.

### Node lookup

Original `0x99c90` tests self first; the Group reference enumerator at
`0x90108` then visits children in their linked order. The native helper
implements that **Node hierarchy** portion with an explicit work vector and
reverse push order. It does not implement general Object3D reference traversal
through animation tracks, appearances, images or other referenced objects.

Construction rejects duplicate/zero/out-of-domain IDs, inconsistent Node type
metadata, dangling or non-Node children, duplicate/multiple parents, children
on non-Group objects and cycles. The entire supplied forest is checked without
recursion. A 20,000-node chain validates and searches successfully. The
1,000,000-object limit and whole-forest validation are explicit native safety
boundaries, not claimed original error-path behavior.

### Explicit loaded-container selection

The original constructor selects child0 as a Group; if that query is null it
tries child1. This is grounded in `0x3e6b4..0x3e756`. The native helper takes an
**explicit ordered child-ID vector** and implements exactly that bounded
selection. It does not identify the original container's class, infer a
container from a World, scan later children, detach nodes or perform
reparenting.

The Group-interface predicate accepts serialized Group9 and World22. The
additive static proof in [M3G_WORLD_GROUP_INTERFACE.md](M3G_WORLD_GROUP_INTERFACE.md)
traces the original public query from `0xb08e0` through World's Type22 branch
at `0xb0a82`, where interface9 is accepted at `0xb0a8e..0xb0a90`. Both types
therefore satisfy the recovered Group-interface requirement. This subtype
finding is static original-code evidence; it adds no new original execution
fixture. Tests cover it at the first/fallback container positions and both
Group positions in a model-camera chain.

### Forms and cameras

Form lookup uses the suffixed names, not the unsuffixed mesh names. It returns
the first matching Node within the explicitly supplied subtree. The helper
does not choose a form or apply rendering enable changes.

Model-camera lookup similarly uses the suffixed `roboCAM` or `vehiCAM` key.
A found transform must expose the Group interface, its child0 must expose the
Group interface, and that object's child0 must be Camera5. The exact accesses
are grounded in `0x3dc9a..0x3dcce` and `0x3df00..0x3df34`. An absent named
transform returns `nullopt`. A found but malformed chain throws `FormatError`;
neither another descendant camera nor a scene camera is substituted.

Camera-ID/phase choice, cache-hit cloning, attachments, reparenting, material
changes and final placement remain outside this module.

## Validation evidence

### Original fixture

The compact generated C++ fixture is derived only from frozen
`m3g_validation/original_bindings.json`, SHA256:

`d6d39b71189bdd1ed6eb53fff342d516f2fabcf4f5d04d534fd2921f6a33300a`

That original run executed unchanged `0x2701c`, `0x26b4c`, `0x99c90`,
`0x90108` and `0x99e1e`, with the recorded allocator/free and C-string host
boundaries. Its six trees are synthetic internal Group objects without
animation references. Expected results are never generated from the native
implementation. Fixture provenance records the original image, runner,
generator and generated-header hashes. The original execution was not repeated
for this implementation pass.

The 43 additional native contracts cover missing/invalid roots, optional
absence, non-child reference exclusion, duplicate-name ordering, malformed
camera chains, exact fallback limits, Group interface subtyping, graph
validation, embedded NUL rejection and deep iterative traversal. The optimized
and UBSan tests each pass 83 checks with empty stderr. Strict compilation uses
C++17, `-Wall -Wextra -Werror -pedantic`.

### Actual original-data corpus

The source-data audit independently parses all 27 resource payloads through
`analysis_tools/m3g/reference.py`, after checking their hashes against the
original extraction manifest and the pinned battle corpus. The four expected
transformed-name keys come directly from the original execution fixture.

| Result | Count |
| --- | ---: |
| Resources | 27 |
| Node lookup queries | 513 |
| First-child Group selections | 14 |
| Second-child Group selections | 13 |
| Present robot-form bindings | 24 |
| Present car-form bindings | 13 |
| Present robot model cameras | 24 |
| Present vehicle model cameras | 13 |
| Field/structure comparisons | 2,677 |
| Differences | 0 |

The three battle scene resources have no model-form or model-camera bindings;
those results remain explicitly absent. The model set contains 24 robot and
13 vehicle bindings, counting special model500. This presence audit does not
impose a substitute vehicle form or camera on models without one.

For **resource490**, the actual serialized World333 has children `[4,332]`,
so the explicit child-list test selects Group332. Robot binding is299; car
binding is **outer331**, before nested330. The robot camera chain is
`15 -> 10 -> 2`, and the vehicle camera chain is `25 -> 20 -> 17`.
These are native/source-data comparisons on the real file, distinct from the
original fixture's synthetic resource490-shaped tree.

Supplying the serialized World child lists to the selection helper is an
explicit **data-shape test**. It does not establish that World is the original
loaded-container class, or that the complete original loader was executed.

Detailed per-resource hashes, IDs, optional results and query digests are in
`m3g_validation/bindings_native_validation.json`.

## Reproduce

From the project root, regenerate the compact fixture from its frozen original
metadata:

```sh
python3 -B analysis_tools/m3g/bindings_validate.py fixture \
  --output native/tests/m3g_bindings_fixture.hpp \
  --provenance native/tests/m3g_bindings_fixture_provenance.json
```

Build the dependency-free binding contracts, then the same contracts with
undefined-behavior instrumentation:

```sh
mkdir -p research/native_m3g/bindings_v1
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I native/include -I native/tests \
  native/src/M3GBindings.cpp native/tests/m3g_bindings_test.cpp \
  -o research/native_m3g/bindings_v1/g1_m3g_bindings_tests
g++ -std=c++17 -O1 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I native/include -I native/tests \
  native/src/M3GBindings.cpp native/tests/m3g_bindings_test.cpp \
  -o research/native_m3g/bindings_v1/g1_m3g_bindings_tests_ubsan
```

Build the actual-data probe using the existing native container/body decoder:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -I native/include \
  native/src/M3GBindings.cpp native/src/M3GScene.cpp native/src/M3G.cpp \
  native/src/Archive.cpp native/tools/m3g_bindings_probe.cpp -lz \
  -o research/native_m3g/bindings_v1/m3g_bindings_probe
python3 -B analysis_tools/m3g/bindings_validate.py audit \
  --probe research/native_m3g/bindings_v1/m3g_bindings_probe \
  --native-test research/native_m3g/bindings_v1/g1_m3g_bindings_tests \
  --ubsan-test research/native_m3g/bindings_v1/g1_m3g_bindings_tests_ubsan \
  --output native/research/m3g_validation/bindings_native_validation.json
```

The actual-data audit uses locally extracted supplied assets. It writes
metadata only. Portable C++ tests require no original IPA, asset payload or
emulator. No Scene, Animation, Frame, Metal, App or CMake source was edited.

## Integration handoff

Add `src/M3GBindings.cpp` to the native library, an optional
`m3g_bindings_probe` tool from `tools/m3g_bindings_probe.cpp`, and a
`g1_m3g_bindings_tests` target from `tests/m3g_bindings_test.cpp`. Suggested
CTest name: `m3g_bindings_original_function_vectors`; the test takes no
arguments. These CMake edits are deliberately left to the integration owner.

Two bounded independent reviews found no concrete hash, hierarchy or camera
chain blocker. The final subtype delta was reviewed against the static World
interface proof. No Apple compilation, GPU rendering or device result is
claimed.
