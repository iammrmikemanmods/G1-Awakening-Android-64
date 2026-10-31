#!/usr/bin/env python3
"""Execute unchanged original support-tutorial and profile-field instructions.

Development only: no ARM interpreter or original code belongs in the native
product. Text construction, message presentation and scalar stream IO are
explicit recording boundaries. This does not execute UI rendering or disk IO.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import sys

IMAGE_SHA256 = "0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd"
MESSAGE_RESOURCE = 0x21FF04A8


def sha(data):
    return hashlib.sha256(data).hexdigest()


class OriginalTutorial:
    def __init__(self, image):
        import unicorn as uc
        from unicorn import arm_const as ar
        self.uc, self.ar = uc, ar
        self.u = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_THUMB)
        self.u.mem_map(0x1000, 0xF0000)
        self.u.mem_write(0x1000, image)
        self.original_text = image[:0xD1000]
        self.u.mem_protect(0x1000, 0xD1000, uc.UC_PROT_READ | uc.UC_PROT_EXEC)
        self.u.mem_map(0x1000000, 0x10000)
        self.u.mem_map(0x2000000, 0x200000)
        self.u.mem_map(0x3000000, 0x1000)
        self.map = 0x1000000
        self.app = 0x1001000
        self.configs = [0x1002000, 0x1002100]
        self.profile_table = 0x1002200
        self.controllers = 0x1002300
        self.helper_slots = 0x1002400
        self.helpers = [0x1003000 + i * 0x100 for i in range(6)]
        self.presenter = 0x1004000
        self.stream = 0x1005000
        self.mode = "tutorial"
        self.events = []
        self.scalars = []
        self.text_ids = {}
        self.w32(self.map + 0x29C, self.app)
        self.w32(self.map + 0x290, self.presenter)
        self.w32(self.map + 0x3C, self.helper_slots)
        self.w32(self.map + 0x40, 6)
        self.w32(self.map + 0x98, self.controllers)
        self.w32(self.map + 0x9C, 256)
        self.w32(self.app + 0x3A8, self.profile_table)
        self.w32(self.app + 0x3B4, 1)
        for i, config in enumerate(self.configs):
            self.w32(self.profile_table + i * 4, config)
        self.u.hook_add(uc.UC_HOOK_CODE, self.hook)

    def w8(self, address, value):
        self.u.mem_write(address, bytes([value & 255]))

    def w32(self, address, value):
        self.u.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def r8(self, address):
        return self.u.mem_read(address, 1)[0]

    def hook(self, instance, address, size, user):
        del size, user
        ar = self.ar
        values = [instance.reg_read(getattr(ar, "UC_ARM_REG_R" + str(i))) for i in range(3)]
        r0, r1, r2 = values
        handled = True
        if address == 0x301AA and self.mode == "tutorial":
            self.text_ids[r0] = r1
            self.events.append(["text_construct", r1, self.r8(self.configs[1] + 0x0E)])
        elif address == 0x46DF2 and self.mode == "tutorial":
            if r0 != self.presenter or r1 not in self.text_ids:
                raise RuntimeError("message presenter or text identity differs")
            self.events.append(["message", self.text_ids[r1], r2,
                                self.r8(self.configs[1] + 0x0E)])
        elif address == 0x604B4 and self.mode == "tutorial":
            self.events.append(["text_destroy", self.text_ids.pop(r0)])
        elif address in (0x3358, 0x339E) and self.mode == "serialize":
            if r0 != self.stream + 0x18:
                raise RuntimeError("unexpected scalar writer identity")
            kind = "byte" if address == 0x3358 else "word"
            self.scalars.append([kind, r1 & (255 if kind == "byte" else 0xFFFFFFFF)])
        elif address in (0x7782, 0x76C4) and self.mode == "deserialize":
            if r0 != self.stream + 8 or not self.input_scalars:
                raise RuntimeError("unexpected scalar reader identity or count")
            kind = "byte" if address == 0x7782 else "word"
            expected, value = self.input_scalars.pop(0)
            if expected != kind:
                raise RuntimeError("original profile scalar read order differs")
            instance.reg_write(ar.UC_ARM_REG_R0, value)
        else:
            handled = False
        if handled:
            instance.reg_write(ar.UC_ARM_REG_PC, instance.reg_read(ar.UC_ARM_REG_LR))
        elif 0xBA1E0 <= address < 0xBAAA0:
            raise RuntimeError(f"unrecorded original import {address:#x}")

    def call(self, entry, stop, args=(), stack_map=False):
        ar = self.ar
        for i in range(13):
            self.u.reg_write(getattr(ar, "UC_ARM_REG_R" + str(i)), 0)
        self.u.mem_write(0x21FE000, bytes(0x2000))
        sp = 0x21FF000
        if stack_map:
            self.w32(sp + 0x14, self.map)
        self.u.reg_write(ar.UC_ARM_REG_CPSR, 0x30)
        self.u.reg_write(ar.UC_ARM_REG_SP, sp)
        self.u.reg_write(ar.UC_ARM_REG_LR, 0x3000001)
        for i, value in enumerate(args):
            self.u.reg_write(getattr(ar, "UC_ARM_REG_R" + str(i)), value)
        self.u.emu_start(entry | 1, stop, count=20000, timeout=1000000)
        if self.u.reg_read(ar.UC_ARM_REG_PC) != stop:
            raise RuntimeError("original function did not reach the exact bounded stop")

    def tutorial(self, values):
        flag, player, controller, *owners = values
        self.mode = "tutorial"
        self.events, self.text_ids = [], {}
        self.u.mem_write(self.configs[0], bytes([0xA5]) * 0x80)
        self.u.mem_write(self.configs[1], bytes([0x5A]) * 0x80)
        self.w8(self.configs[1] + 0x0E, flag)
        self.w8(self.map + 0xA8, player)
        self.u.mem_write(self.controllers, bytes([0x7F]) * 256)
        self.w8(self.controllers + player, controller)
        # The branch scans the allocated six slots; it does not read count.
        self.w32(self.map + 0x44, 0)
        for i, owner in enumerate(owners):
            self.u.mem_write(self.helpers[i], bytes([0x35 + i]) * 0x80)
            self.w32(self.helper_slots + i * 4, 0 if owner == -1 else self.helpers[i])
            if owner != -1:
                self.w8(self.helpers[i] + 0x0A, owner)
        snapshot = bytes(self.u.mem_read(0x1000000, 0x6000))
        self.call(0x58796, 0x5887E, stack_map=True)
        new_flag = self.r8(self.configs[1] + 0x0E)
        expected = bytearray(snapshot)
        expected[self.configs[1] + 0x0E - 0x1000000] = new_flag
        if bytes(self.u.mem_read(0x1000000, 0x6000)) != expected:
            raise RuntimeError("tutorial changed state outside the selected profile flag")
        if self.events and self.events != [
                ["text_construct", MESSAGE_RESOURCE, 0],
                ["message", MESSAGE_RESOURCE, 1, 0],
                ["text_destroy", MESSAGE_RESOURCE]]:
            raise RuntimeError("tutorial message ordering, resource, mode or flag timing differs")
        if self.text_ids:
            raise RuntimeError("original temporary text was not destroyed")
        return [new_flag, int(bool(self.events)), MESSAGE_RESOURCE if self.events else 0]

    def profile_prefixes(self):
        config = self.configs[1]
        self.mode = "default"
        self.u.mem_write(config, bytes([0xA5]) * 0x80)
        self.call(0x440F2, 0x4410A, [config])
        default_flag = self.r8(config + 0x0E)
        if default_flag != 1:
            raise RuntimeError("original profile default is not pending")
        results = []
        for flag in range(256):
            self.u.mem_write(config, bytes(0x80))
            for offset, value in [(0, 0x31), (0x0C, 0x42), (0x0D, 0x53), (0x0E, flag), (0x0F, 0x64)]:
                self.w8(config + offset, value)
            self.w32(config + 4, 0x12345678)
            self.w32(config + 8, 0xABCDEF01)
            self.scalars = []
            self.mode = "serialize"
            self.call(0x443CA, 0x4441E, [config, self.stream])
            expected = [["byte", 0x31], ["word", 0x12345678], ["word", 0xABCDEF01],
                        ["byte", 0x42], ["byte", flag], ["byte", 0x64], ["byte", 0x53]]
            if self.scalars != expected:
                raise RuntimeError("original profile field order differs")
            self.input_scalars = [row[:] for row in self.scalars]
            self.u.mem_write(config, bytes([0xA5]) * 0x80)
            self.mode = "deserialize"
            self.call(0x4448A, 0x4451A, [config, self.stream])
            normalized = self.r8(config + 0x0E)
            if self.input_scalars or normalized != int(flag != 0):
                raise RuntimeError("original profile flag read normalization differs")
            results.append({"input_flag": flag, "serialized_flag": self.scalars[4][1],
                            "restored_flag": normalized})
        return {"default_pending_flag": default_flag, "roundtrips": results}

    def check_text(self):
        if bytes(self.u.mem_read(0x1000, 0xD1000)) != self.original_text:
            raise RuntimeError("original executable text changed")


def cases():
    rows = set()
    for player in (0, 1):
        other = 1 - player
        patterns = {(-1,) * 6, (other,) * 6, (player,) * 6,
                    tuple(player if i % 2 else other for i in range(6))}
        for slot in range(6):
            for fill in (-1, other, 255):
                pattern = [fill] * 6
                pattern[slot] = player
                patterns.add(tuple(pattern))
        for flag, controller, owners in itertools.product(
                (0, 1, 2, 127, 128, 255), (-128, -1, 0, 1, 2, 127), sorted(patterns)):
            rows.add((flag, player, controller, *owners))
        for mask in range(64):
            owners = tuple(player if (mask >> i) & 1 else other for i in range(6))
            rows.add((1, player, 0, *owners))
    for flag in range(256):
        rows.add((flag, 0, 0, -1, -1, -1, -1, -1, 0))
    for controller in range(-128, 128):
        rows.add((1, 1, controller, -1, -1, -1, -1, -1, 1))
    # Prepared synthetic ownership bytes beyond the normal two-player range
    # verify that the original comparison is byte equality, not truthiness.
    for player in (2, 127, 128, 255):
        rows.add((1, player, 0, -1, -1, -1, -1, -1, player))
        rows.add((1, player, 0, -1, -1, -1, -1, -1, player - 1))
    return sorted(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "unicorn-dir", "output", "fixture"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.unicorn_dir))
    import unicorn
    image = args.binary.read_bytes()
    if sha(image) != IMAGE_SHA256:
        raise ValueError("original executable hash mismatch")
    original = OriginalTutorial(image)
    rows = []
    for values in cases():
        rows.append({"input": list(values), "output": original.tutorial(values)})
    original.check_text()
    prefixes = original.profile_prefixes()
    original.check_text()
    fixture = ["# Original 0x58796..0x5887e, unchanged executable; recording-only UI boundaries.",
               "# pending current_player signed_controller six_owner_slots(-1=null) | next_pending message_count resource"]
    fixture += [" ".join(map(str, row["input"])) + " | " + " ".join(map(str, row["output"])) for row in rows]
    fixture_bytes = ("\n".join(fixture) + "\n").encode()
    report = {
        "schema": 1, "kind": "original support tutorial predicate and profile-field recovery",
        "original_image_sha256": sha(image), "oracle_sha256": sha(Path(__file__).read_bytes()),
        "unicorn_version": unicorn.__version__, "instruction_patches": 0,
        "original_code_readonly_unchanged": True, "tutorial_cases": len(rows),
        "tutorial_output_scalars": len(rows) * 3,
        "requested_message_cases": sum(row["output"][1] for row in rows),
        "tutorial_entry": "0x58796", "tutorial_stop_before_epilogue": "0x5887e",
        "actual_accessors_executed": ["0x2b5e6 selected configuration", "0x2aa78 controller vector"],
        "tutorial_boundaries": ["0x301aa text construction records resource identity",
            "0x46df2 message presentation records text, mode and already-cleared flag",
            "0x604b4 temporary text disposal"],
        "profile_prefixes": prefixes,
        "profile_prefix_ranges": {"default_reset": "0x440f2..0x4410a",
            "serialize_fields": "0x443ca..0x4441e", "deserialize_fields": "0x4448a..0x4451a"},
        "profile_boundaries": ["0x3358 byte writer", "0x339e word writer",
            "0x7782 byte reader", "0x76c4 word reader"],
        "profile_io_scope": "Scalar field order/normalization only; no original disk read/write or full profile records executed.",
        "state_mutation_scope": "Every tutorial case permits only selected config+0x0e mutation in the prepared app/map/units region.",
        "default_reset_caller_review": {"constructor": "0x44360 calls 0x440f2",
            "selected_profile_reset": "0x30560 calls 0x440f2; 0x3058e calls selected-profile save 0x2fd74",
            "scope": "Instruction reviewed; full caller and filesystem path not executed."},
        "fixture_sha256": sha(fixture_bytes), "gpu_execution": False,
        "original_popup_execution": False, "original_disk_io_execution": False,
        "physical_device_execution": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.fixture.parent.mkdir(parents=True, exist_ok=True)
    args.fixture.write_bytes(fixture_bytes)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"tutorial_cases": len(rows), "scalar_comparisons": len(rows) * 3,
                      "profile_prefix_roundtrips": len(prefixes["roundtrips"]), "passed": True}))


if __name__ == "__main__":
    main()
