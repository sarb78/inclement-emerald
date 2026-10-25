#!/usr/bin/env python3
"""Compile the production preview and its battle calculations as a host C test.

Run with Python 3 and GCC (on Windows, use the Ubuntu WSL interpreter). No ROM
is built and no game or save is launched. Only item metadata and knowledge/event
recording are fixture adapters; move/type tables, dynamic typing, abilities,
grounding, target resolution, priority and preview functions come from the game.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def extract(path, name, data=False):
    source = (ROOT / path).read_text(encoding="utf-8")
    if data:
        pattern = rf"^[^\n;]*\b{re.escape(name)}\s*\[[^\n]*=\s*\{{"
    else:
        pattern = rf"^[\w *]+\b{re.escape(name)}\([^;]*?\)\s*\{{"
    match = re.search(pattern, source, re.M)
    if not match:
        raise RuntimeError(f"Cannot find production definition: {path}:{name}")
    # Mask strings/comments while preserving offsets, so comment braces cannot
    # change the extracted function's extent.
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: " " * len(m[0]), source)
    depth = 0
    opening = source.index("{", match.start())
    for end in range(opening, len(source)):
        depth += (masked[end] == "{") - (masked[end] == "}")
        if depth == 0:
            return source[match.start():end + 1] + (";" if data else "")
    raise RuntimeError(f"Unterminated production definition: {name}")


def main():
    util = "src/battle_util.c"
    definitions = ["#define X UQ_4_12"]
    for name in ("sAbilitiesAffectedByMoldBreaker", "sTypeEffectivenessTable",
                 "sInverseTypeEffectivenessTable", "gNaturalGiftTable"):
        definitions.append(extract(util, name, data=True))
    for path, names in (
        ("src/battle_anim_mons.c", ("GetBattlerSide", "GetBattlerPosition", "GetBattlerAtPosition")),
        (util, ("IsNeutralizingGasOnField", "IsNeutralizingGasBannedAbility", "GetBattlerAbility",
                "IsAbilityOnField", "GetBattlerHoldEffect", "GetBattlerHoldEffectParam",
                "IsBattlerGrounded", "IsBattlerAlive", "IsBattlerTerrainAffected", "MulModifier",
                "MulByTypeEffectiveness", "UpdateMoveResultFlags", "CalcTypeEffectivenessMultiplierInternal",
                "CalcTypeEffectivenessMultiplier", "GetTypeModifier")),
        ("src/battle_main.c", ("GetMovePriority", "SetTypeBeforeUsingMove")),
        (util, ("IsMovePreviewBlocked", "GetMovePreviewTargets", "GetMoveEffectivenessPreview")),
    ):
        definitions.extend(extract(path, name) for name in names)
    fixture = (ROOT / "tests/move_effectiveness_host.c").read_text(encoding="utf-8")
    code = fixture.replace("/* PRODUCTION CODE */", "\n\n".join(definitions))
    with tempfile.TemporaryDirectory(prefix="inclement-effectiveness-") as folder:
        source = Path(folder) / "effectiveness.c"
        binary = Path(folder) / "effectiveness"
        source.write_text(code, encoding="utf-8")
        subprocess.run([os.environ.get("CC", "gcc"), "-std=gnu11", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-Werror=implicit-function-declaration", "-Werror=return-type",
                        "-Wno-pointer-to-int-cast", "-fmax-errors=8",
                        "-iquote", str(ROOT / "include"), "-iquote", str(ROOT / "gflib"),
                        "-iquote", str(ROOT), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
