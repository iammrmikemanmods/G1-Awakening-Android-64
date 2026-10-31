#!/usr/bin/env python3
"""Compare the unchanged original 2D overlay clock with SpriteAnimation.

This executes 0x5e238 (start) and 0x5ea80 (advance), not the already-recovered
3D sprite player. The original 2D clock stores reverse at +0x7c, and has no
geometry-refresh return value. Allocation/free and byte initialization are
the only substituted boundaries. No parser, drawing, GPU or device claim is
made. The descriptor JSON must come from the separately verified v1 decoder.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys


IMAGE_SHA = "0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd"
DESCRIPTOR_SHA = "dc520143e30346dad8ed479089fcb78783b97922b7e1bda0eef0e5c1f28a50bb"


def sha(value):
    return hashlib.sha256(value).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "unicorn-dir", "base-oracle", "descriptor-json",
                 "native-probe", "native-probe-source", "native-player-source", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    image = args.binary.read_bytes()
    if sha(image) != IMAGE_SHA:
        raise ValueError("original executable differs from the audited input")
    descriptor = json.loads(args.descriptor_json.read_text())
    if (descriptor["source_sha256"] != DESCRIPTOR_SHA or descriptor["version"] != 1 or
            descriptor["bytes_consumed"] != 546 or len(descriptor["animations"]) != 4):
        raise ValueError("descriptor fixture differs from the audited resource 256")
    sys.path.insert(0, str(args.unicorn_dir))
    import unicorn
    spec = importlib.util.spec_from_file_location("support_animation_base", args.base_oracle)
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)

    class Original2DAnimation(base.OriginalAnimation):
        def hook(self, instance, address, size, user):
            if address == 0x728f0:
                raise RuntimeError("2D clock unexpectedly entered the 3D geometry boundary")
            super().hook(instance, address, size, user)

        def state2d(self, sprite):
            index = self.read_word(sprite + 0x64)
            count = self.read_word(sprite + 0x60)
            if not 0 <= index < count:
                raise RuntimeError("original 2D animation escaped the seeded sequence")
            frames = self.read_word(sprite + 0x5c)
            return [index, self.u.mem_read(frames + index, 1)[0],
                    self.read_word(sprite + 0x78, True), self.read_word(sprite + 0x70, True),
                    self.u.mem_read(sprite + 0x7c, 1)[0], self.u.mem_read(sprite + 0x75, 1)[0]]

        def run(self, case, through_map_scheduler=False):
            self.next = 0x1000000
            sprite = self.alloc(0xa8)
            frames_outer, durations_outer = self.alloc(24), self.alloc(24)
            frames = self.alloc(len(case["steps"]))
            durations = self.alloc(2 * len(case["steps"]))
            self.u.mem_write(frames, bytes(step[0] for step in case["steps"]))
            self.u.mem_write(durations, struct.pack("<" + "h" * len(case["steps"]),
                                                   *(step[1] for step in case["steps"])))
            # Select animation 2 for every prepared sequence; the original
            # vector access/copy and duration lookup still execute unchanged.
            self.vector(frames_outer + 16, frames, len(case["steps"]))
            self.vector(durations_outer + 16, durations, len(case["steps"]))
            self.vector(sprite + 0x48, frames_outer, 3)
            self.vector(sprite + 0x50, durations_outer, 3)
            renderer, effect_bank = self.alloc(0x70), self.alloc(4)
            self.word(effect_bank, sprite)
            self.word(renderer + 0x28, effect_bank)
            self.word(renderer + 0x6c, 1)
            self.call(0x5e238, [sprite, 2, case["repeat"], case["reverse"]])
            states = [self.state2d(sprite)]
            for tick, delta in enumerate(case["deltas"]):
                try:
                    if through_map_scheduler:
                        self.call(0x5ca44, [renderer, delta])
                    else:
                        self.call(0x5ea80, [sprite, delta])
                except Exception as error:
                    raise RuntimeError(f"{case['name']} repeat={case['repeat']} reverse={case['reverse']} "
                                       f"tick={tick} delta={delta} prior={states[-1]}") from error
                states.append(self.state2d(sprite))
            if bytes(self.u.mem_read(0x1000, 0xd1000)) != self.text:
                raise RuntimeError("original executable instructions changed")
            return states

    sequences = [(f"resource256/animation{index}", steps)
                 for index, steps in enumerate(descriptor["animations"])]
    sequences += [("synthetic-single", [[255, 1]]),
                  ("synthetic-unequal", [[7, 100], [8, 30], [9, 200]]),
                  ("synthetic-duration-boundary", [[0, 32767], [255, 1], [127, 2]])]
    cases = []
    for name, steps in sequences:
        for repeat in (-1, 0, 1, 3):
            for reverse in (0, 1):
                duration = steps[-1 if reverse else 0][1]
                cases.append({"name": name, "steps": steps, "repeat": repeat, "reverse": reverse,
                              "deltas": [0, duration - 1, 1, 16, 17, 30, 100,
                                         min(4096, max(step[1] for step in steps) * 3 + 1), 1000, 0]})
    cases.append({"name": "support-animation2-direct-clock-through-2010ms", "steps": descriptor["animations"][2],
                  "repeat": 1, "reverse": 0, "deltas": [30] * 67})
    lines = [str(len(cases))]
    for case in cases:
        values = [len(case["steps"]), case["repeat"], case["reverse"], len(case["deltas"])]
        values += [value for step in case["steps"] for value in step]
        values += case["deltas"]
        lines.append(" ".join(map(str, values)))
    native = subprocess.run([str(args.native_probe.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30)
    parsed = [json.loads(line) for line in native.stdout.splitlines()]
    if len(parsed) != len(cases):
        raise RuntimeError("native probe returned the wrong number of cases")
    actual = [[state[:6] for state in states] for states in parsed]
    original = Original2DAnimation(image)
    expected = []
    for case, states in zip(cases, actual):
        reference = original.run(case)
        if reference != states:
            raise RuntimeError(json.dumps({"case": case, "original": reference, "native": states}))
        expected.append(reference)
    support_states = expected[-1]
    first_complete_tick = next(index for index, state in enumerate(support_states) if state[5])
    scheduler_states = original.run(cases[-1], through_map_scheduler=True)
    if (scheduler_states[:first_complete_tick + 1] != support_states[:first_complete_tick + 1] or
            any(state != support_states[first_complete_tick]
                for state in scheduler_states[first_complete_tick:])):
        raise RuntimeError("original map effect scheduler did not preserve and freeze the completed 2D clock")
    report = {
        "schema": 1,
        "scope": "Existing portable SpriteAnimation versus unchanged original 2D overlay start/advance",
        "original_image_sha256": IMAGE_SHA,
        "descriptor_resource": 256, "descriptor_sha256": DESCRIPTOR_SHA,
        "descriptor_json_sha256": sha(args.descriptor_json.read_bytes()),
        "oracle_sha256": sha(Path(__file__).read_bytes()),
        "base_oracle_sha256": sha(args.base_oracle.read_bytes()),
        "native_probe_source_sha256": sha(args.native_probe_source.read_bytes()),
        "native_player_source_sha256": sha(args.native_player_source.read_bytes()),
        "native_probe_binary_sha256": sha(args.native_probe.read_bytes()),
        "unicorn_version": unicorn.__version__,
        "original_functions": ["0x5e238", "0x5ea80", "0x5ca44 map effect scheduler"],
        "instruction_patches": 0, "original_code_readonly_and_unchanged": True,
        "boundaries": ["allocation/free", "byte buffer initialization"],
        "boundary_calls": dict(original.calls),
        "state_fields": ["step", "frame", "elapsed_ms", "repeat_count", "reverse", "complete"],
        "ignored_native_fields": ["3D geometry-refresh boolean; the 2D routine has no corresponding field"],
        "original_sequences": 4, "synthetic_sequences": 3, "case_count": len(cases),
        "state_count": sum(len(states) for states in expected),
        "scalar_comparisons": sum(len(states) * 6 for states in expected),
        "support_fixed30ms": {"first_complete_tick": first_complete_tick,
                               "first_complete_elapsed_ms": first_complete_tick * 30,
                               "phase_handoff_tick": 67, "phase_handoff_elapsed_ms": 2010,
                               "scheduler_final_state_at_handoff": scheduler_states[-1],
                               "scheduler_stops_at_completion": True,
                               "scheduler_original_states": scheduler_states,
                               "direct_player_continuation_state_at_2010ms": support_states[-1]},
        "cases": [dict(case, original_states=states) for case, states in zip(cases, expected)],
        "limitations": ["Prepared decoded sequences; parser accuracy is a separate oracle",
                        "No image, projection, graphics API, UIKit, Metal or device execution",
                        "Native safe-input contract rejects negative/overflowing deltas; this comparison uses valid nonnegative clock inputs",
                        "The 2D clock does not determine support-phase handoff; original 0x583f0 uses its own >1999 ms clock"],
        "passed": True,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    summary = {key: report[key] for key in ("case_count", "state_count", "scalar_comparisons", "passed")}
    summary["support_fixed30ms"] = {key: value for key, value in report["support_fixed30ms"].items()
                                    if key != "scheduler_original_states"}
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
