"""Refresh the modlock profile manifest from an installed Deadlock copy.

Hashes the hooked modules (server.dll, engine2.dll, tier0.dll) in an installed
game directory and prints one JSON record per module. The records feed the
kProfileModules table in include/modlock/gameinterop/profile_manifest.h, which
is reviewed and committed by hand: hashing proves which build was measured; the
review records why its scan results are trusted.

Usage::

    python3 scripts/generate_profile_manifest.py --game-dir "C:/Program Files (x86)/Steam/steamapps/common/Deadlock"
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

MODULES = [
    ("server", "game/citadel/bin/win64/server.dll"),
    ("engine", "game/bin/win64/engine2.dll"),
    ("tier0", "game/bin/win64/tier0.dll"),
]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True, help="installed Deadlock directory")
    args = parser.parse_args()
    game_dir = Path(args.game_dir)
    for role, relative in MODULES:
        path = game_dir / relative
        if not path.is_file():
            raise SystemExit(f"missing module: {path}")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        print(json.dumps({"role": role, "path": relative, "sha256": digest,
                          "size": path.stat().st_size}))


if __name__ == "__main__":
    main()
