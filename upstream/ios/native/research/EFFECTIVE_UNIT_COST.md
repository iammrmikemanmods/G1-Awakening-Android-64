# Effective unit cost correction

The native cost readers now consistently use the table value produced by the
original loader. `World::unit_spawn_cost` already modeled that value for spawn;
upgrade eligibility/debit and AI upgrade/production selection now share it.
Raw decoded resource bytes remain unchanged.

## Original evidence and defect

The original loader writes scalar1 to map vector`+0x14c`, then at
`0x5a8ce..0x5a8e0` compares the current type with21 and writes40 for that type.
Other types retain the raw byte, interpreted as signed by the later readers.
Actual type21 resource297 has raw scalar1=25; using it directly therefore
underprices that type by15. Its resource SHA256 is
`c95f026a2b7ef3247639f63c0da375cce2d080324d7258796909f4016e701f2c`.

The earlier UpgradeAction and AI source copied the raw-byte interpretation
instead of calling the already correct helper. Their synthetic original-code
harness also seeded map+0x14c directly from the raw value, so those comparisons
did not exercise the loader override. The correction applies to both native
consumers and the reference input construction. Earlier frozen reports remain
historical evidence for their recorded input states; they do not prove the
omitted type21 loader behavior.

The new loader check executes the unchanged original block for all23 types and
raw bytes0,1,5,40,127,128,255: **161 executions pass**. No override instruction
or vector-index helper is replaced. The result is40 for type21 and the supplied
raw byte for every other type. The main original image remains
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`;
original libgcc remains
`d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83`.

## Behavioral checks

Three independent original consumers have explicit boundary scenarios using
raw costs0,5,255 and funds39,40,41:

| Consumer |39 credits |40 credits |41 credits |
|---|---|---|---|
| Human upgrade eligibility/debit | Rejected | Allowed; remaining0 | Allowed; remaining1 |
| AI fallback upgrade selection | Ordinary movement action | Upgrade action4 | Upgrade action4 |
| AI production selection | No type21 production request | Type21 request | Type21 request |

All nine combinations for each consumer match native execution. This tests
signed raw values too: type21 never becomes a negative/free price when the raw
byte is255. Other types retain the original signed and wrapping behavior.

All affected varied corpora were regenerated from original execution with the
corrected map input. The actual-definition and previously unchanged attack
corpora were regenerated as well. Native strict builds and the undefined-
behavior sanitizer both pass:

| Corpus |Cases |Checks |
|---|---:|---:|
| Upgrade/repair varied plus cost boundaries |201|3,552|
| Upgrade/repair actual definitions |92|1,616|
| AI attack/score varied |192|6,479|
| AI fallback/combined plus cost boundaries |137|8,050|
| AI idle/production plus cost boundaries |201|15,633|
| AI attack actual definitions |92|20,718|
| AI first-map planner scenarios |8|1,994|

There are923 fixture cases and58,042 native checks in each build mode. The
original161 loader-prefix executions are additional independent evidence. This
is function-level validation, not a full turn, campaign, live save or device test.

The repository-wide native source search leaves raw scalar1 interpretation in
the shared effective-cost helper; UpgradeAction, AI, UnitSpawn and
UnitInteractions call that helper. UnitInteractions is independently owned and
its production validation is recorded separately.

## Records and reproduction

`unit_action_validation/effective_cost_correction.json` records previous report
hashes, the exact new source/fixture hashes, loader results, explicit boundaries,
strict/sanitized commands and the regeneration commands. Refreshed summaries
are `unit_action_validation/original_unit_actions.json`,
`unit_action_validation/native_unit_actions.json`,
`ai_validation/original_ai_oracle.json` and
`ai_validation/native_ai_validation.json`.

Use the commands in `UPGRADE_HEAL_ACTIONS.md` and
`analysis_tools/ai/README.md`. Planner/action/turn tools append nine cost cases
to the requested original varied count. The original-code harness remains a
development tool only. Normal CMake fixture tests use native source and
synthetic expected values; original resources, machine-code inputs, source-data
fixtures and compiled local probes are excluded from the source checkpoint.
