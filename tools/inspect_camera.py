#!/usr/bin/env python3
"""Sample EDF6's spectator direction on Linux/Proton without writing game memory.

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
        head = self.ptr(self.ptr(self.ptr(self.base + 0x20B2978) + 0x38))
        pending = [self.ptr(head + 8)]
        visited = set()
        players = []
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
            if self.ptr(player) - self.base not in (
                    0x17CDF28, 0x17CF100, 0x17CF5B8, 0x17D0FF8, 0x17D24D8):
                continue
            net = self.ptr(player + 0x1ED0)
            if not net:
                continue
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
    args = parser.parse_args()
    game = Game(args.pid)
    try:
        end = time.monotonic() + args.duration
        while time.monotonic() < end:
            try:
                sample = game.sample()
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
