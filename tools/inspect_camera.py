#!/usr/bin/env python3
"""Sample EDF6's spectator direction on Linux/Proton without writing game memory.

With --weapons, sample each player's weapon list and Wing Diver energy instead,
the data the game's weapon HUD would read for a remote player.

Run as a user permitted to read the game's /proc/PID/mem. Output is JSON Lines.
Samples are not atomic: the game can advance between reads. Compare sustained
differences, not a single frame. Field evidence is in docs/spectate/research.md.
"""

import argparse
import json
import math
import os
from pathlib import Path
import struct
import time

# Player class vtables (RVAs).
CLASSES = {0x17CDF28: "Ranger", 0x17D0FF8: "Wing Diver", 0x17CF100: "Air Raider", 0x17CF5B8: "Fencer",
           0x17D24D8: "SoldierBase"}


class Game:
    def __init__(self, pid):
        maps = Path(f"/proc/{pid}/maps").read_text().splitlines()
        images = [int(line.split("-", 1)[0], 16) for line in maps
                  if line.endswith("/EDF.dll") and line.split()[2] == "00000000"]
        if len(images) != 1:
            raise ValueError("expected one mapped EDF.dll with file offset zero")
        self.base = images[0]
        self.fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
        try:
            nt = self.base + self.u32(self.base + 0x3C)
            if (self.read(self.base, 2) != b"MZ" or self.read(nt, 4) != b"PE\0\0"
                    or self.u32(nt + 8) != 0x678CCB46
                    or self.u32(nt + 0x50) != 0x22CE000):
                raise ValueError("unsupported EDF.dll headers")
        except Exception:
            self.close()
            raise

    def close(self):
        os.close(self.fd)

    def read(self, address, size):
        data = os.pread(self.fd, size, address)
        if len(data) != size:
            raise ValueError("short memory read")
        return data

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def ptr(self, address):
        return struct.unpack("<Q", self.read(address, 8))[0]

    def floats(self, address, count=4):
        values = struct.unpack("<" + "f" * count, self.read(address, count * 4))
        if not all(math.isfinite(value) for value in values):
            raise ValueError("non-finite direction or health data")
        return values

    def live(self, control):
        return control and 0 < self.u32(control + 8) < 0x80000000

    def players(self):
        """(SoldierBase, network record) for each network player in TeamManager's map."""
        head = self.ptr(self.ptr(self.ptr(self.base + 0x20B2978) + 0x38))
        pending = [self.ptr(head + 8)]
        visited = set()
        while pending:
            node = pending.pop()
            if self.read(node + 0x19, 1) != b"\0":
                continue
            if node in visited or len(visited) >= 64:
                raise ValueError("player tree changed or exceeded its traversal limit")
            visited.add(node)
            pending.extend((self.ptr(node), self.ptr(node + 0x10)))
            player = self.ptr(node + 0x20)
            # Only the known player class vtables have the SoldierBase fields below.
            if self.ptr(player) - self.base not in CLASSES:
                continue
            net = self.ptr(player + 0x1ED0)
            if net:
                yield player, net

    def type_name(self, obj):
        """MSVC RTTI name of a polymorphic object in EDF.dll, or its vtable RVA."""
        vtable = self.ptr(obj)
        try:
            locator = self.ptr(vtable - 8)
            descriptor = self.base + self.u32(locator + 12)
            return self.read(descriptor + 16, 64).split(b"\0", 1)[0].decode("ascii")
        except (OSError, ValueError, UnicodeDecodeError):
            return hex(vtable - self.base)

    def count(self, address, limit, size=4):
        value = struct.unpack("<I" if size == 4 else "<Q", self.read(address, size))[0]
        if value > limit:
            raise ValueError(f"count {value} at {address:#x} is over {limit}")
        return value

    def weapon(self, obj):
        """The fields WeaponBase's HUD status (vtable slot 27, 692100) reads."""
        if not obj:
            return None
        def i32(offset):
            return struct.unpack("<i", self.read(obj + offset, 4))[0]

        def f32(offset):
            value = struct.unpack("<f", self.read(obj + offset, 4))[0]
            return value if math.isfinite(value) else None

        return {
            "object": hex(obj),
            "type": self.type_name(obj),
            "i_0xBE8": i32(0xBE8), "i_0x248": i32(0x248), "i_0x20C": i32(0x20C),
            "i_0xE68": i32(0xE68), "i_0x798": i32(0x798),
            "f_0x22C": f32(0x22C), "f_0xE7C": f32(0xE7C), "f_0xE0C": f32(0xE0C),
            "b_0xDD0": self.read(obj + 0xDD0, 1)[0], "q_0xF18": hex(self.ptr(obj + 0xF18)),
        }

    def weapon_sample(self):
        players = []
        for player, net in self.players():
            # SoldierBase: weapon objects at +0x1950 (count +0x1960), weapon records of 0x150 bytes at
            # +0x1970 (count +0x1980; the weapon object at +0x40), sub-weapon slots of 0x20 bytes at
            # +0x1A00 (count +0x1A10; an index into +0x1950 at +0).
            objects = self.ptr(player + 0x1950)
            records = self.ptr(player + 0x1970)
            subs = self.ptr(player + 0x1A00)
            info = {
                "slot": self.u32(net + 0x48),
                "object": hex(player),
                "remote": bool(self.u32(player + 0x128) & 1),
                "class": CLASSES[self.ptr(player) - self.base],
                # The weapon HUD's energy gauge (8353D0) reads +0x2FC (1: emergency charge), +0x304 and +0x308.
                "energy": self.floats(player + 0x308, 1)[0],
                "energy_max": self.floats(player + 0x304, 1)[0],
                "emergency_charge": self.u32(player + 0x2FC),
                "weapon_objects": [self.weapon(self.ptr(objects + 8 * i))
                                   for i in range(self.count(player + 0x1960, 16))],
                "weapon_records": [{
                    "variant": struct.unpack("<H", self.read(records + 0x150 * i + 0x18, 2))[0],
                    "i_0x38": self.u32(records + 0x150 * i + 0x38),
                    "weapon": hex(self.ptr(records + 0x150 * i + 0x40)),
                } for i in range(self.count(player + 0x1980, 16))],
                "sub_weapon_indices": [struct.unpack("<i", self.read(subs + 0x20 * i, 4))[0]
                                       for i in range(self.count(player + 0x1A10, 16, 8))],
            }
            if info["class"] == "Wing Diver":
                # The Wing Diver's player record element (mask 0x800, 582E40) carries these three.
                info["synced_0x2360"] = self.floats(player + 0x2360, 1)[0]
                info["synced_0x2364"] = self.u32(player + 0x2364)
                info["synced_0x2478"] = self.read(player + 0x2478, 1)[0]
            players.append(info)
        return {"state": "sampled", "players": sorted(players, key=lambda player: player["slot"])}

    def sample(self):
        system = self.ptr(self.base + 0x20B2958)
        camera = self.ptr(system + 0x4D8)
        if not camera or not self.live(self.ptr(system + 0x4E0)):
            return {"state": "no current camera"}
        if self.ptr(camera) != self.base + 0x1768C10:
            return {"state": "current camera is not CharacterGhostCamera"}
        if not self.live(self.ptr(camera + 0x358)):
            return {"state": "camera target expired"}
        target = self.ptr(camera + 0x350)
        forward = self.floats(camera + 0x240)
        cached = self.floats(camera + 0x390)
        players = []
        for player, net in self.players():
            look = self.floats(player + 0x1230)
            actor = self.floats(player + 0x80)
            pitch, yaw = look[:2]
            received_forward = (math.sin(yaw) * math.cos(pitch), -math.sin(pitch),
                                math.cos(yaw) * math.cos(pitch))
            watched = player == target
            players.append({
                "slot": self.u32(net + 0x48),
                "object": hex(player),
                "remote": bool(self.u32(player + 0x128) & 1),
                "health": self.floats(player + 0x2F8, 1)[0],
                "watched": watched,
                "look_angles": look,
                "body_angles": self.floats(player + 0x1240),
                "actor_forward": actor,
                "alternate_camera_params": self.read(player + 0x224, 1)[0],
                "actor_vs_look_degrees": angle(actor, received_forward),
                "camera_vs_look_degrees": angle(forward, received_forward) if watched else None,
            })
        if self.ptr(system + 0x4D8) != camera or self.ptr(camera + 0x350) != target:
            raise ValueError("camera changed during the sample")
        return {
            "state": "sampled",
            "camera": hex(camera),
            "target": hex(target),
            "camera_forward": forward,
            "cached_target_forward": cached,
            "camera_vs_cached_degrees": angle(forward, cached),
            "explicit_rotation": self.read(camera + 0x3F0, 1)[0],
            "camera_angles": self.floats(camera + 0x400),
            "aim_correction": self.read(camera + 0x301, 1)[0],
            "players": sorted(players, key=lambda player: player["slot"]),
        }


def angle(a, b):
    length = math.sqrt(sum(v * v for v in a[:3]) * sum(v * v for v in b[:3]))
    if length == 0:
        return None
    cosine = sum(x * y for x, y in zip(a[:3], b[:3])) / length
    return math.degrees(math.acos(max(-1, min(1, cosine))))


def positive(value):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int, help="running EDF6.exe process ID")
    parser.add_argument("--duration", type=positive, default=30, help="seconds to capture")
    parser.add_argument("--interval", type=positive, default=0.1, help="seconds between samples")
    parser.add_argument("--weapons", action="store_true",
                        help="sample weapon lists and Wing Diver energy instead of the camera")
    args = parser.parse_args()
    game = Game(args.pid)
    try:
        end = time.monotonic() + args.duration
        while time.monotonic() < end:
            try:
                sample = game.weapon_sample() if args.weapons else game.sample()
            except (OSError, ValueError) as error:
                sample = {"state": "unavailable", "reason": str(error)}
            print(json.dumps({"time": time.time(), **sample}, allow_nan=False), flush=True)
            time.sleep(min(args.interval, max(0, end - time.monotonic())))
    except KeyboardInterrupt:
        pass
    finally:
        game.close()


if __name__ == "__main__":
    main()
